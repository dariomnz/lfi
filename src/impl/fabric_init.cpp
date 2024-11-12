
/*
 *  Copyright 2024-2025 Dario Muñoz Muñoz, Felix Garcia Carballeira, Diego Camarmas Alonso, Alejandro Calderon Mateos
 *
 *  This file is part of LFI.
 *
 *  LFI is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  LFI is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with LFI.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "impl/fabric.hpp"
#include "impl/socket.hpp"
#include "impl/debug.hpp"
#include "impl/ns.hpp"

#include <cstring>

namespace LFI
{
    LFI::~LFI()
    {
        debug_info("[LFI] Start");
        destroy_thread_cq();

        if (shm_ep.initialized()){
            destroy(shm_ep);            
        }
        if (peer_ep.initialized()){
            destroy(peer_ep);            
        }
        debug_info("[LFI] End");
    }

    int LFI::set_hints(fabric_ep &fabric_ep, const std::string& prov)
    {
        debug_info("[LFI] Start");
        LFI &lfi = LFI::get_instance();
        std::unique_lock<std::mutex> lock(lfi.m_init_mutex);

        fabric_ep.hints = fi_allocinfo();
        if (!fabric_ep.hints)
            return -FI_ENOMEM;

        fabric_ep.hints->ep_attr->type = FI_EP_RDM;

        fabric_ep.hints->caps = FI_MSG | FI_TAGGED;

        fabric_ep.hints->tx_attr->op_flags = FI_DELIVERY_COMPLETE;

        fabric_ep.hints->mode = FI_CONTEXT;

        fabric_ep.hints->domain_attr->threading = FI_THREAD_SAFE;

        if (!prov.empty())
            fabric_ep.hints->fabric_attr->prov_name = strdup(prov.c_str());

        debug_info("[LFI] End");

        return 0;
    }

    int LFI::init(fabric_ep &fabric_ep, bool have_threads)
    {
        int ret;
        struct fi_cq_attr cq_attr = {};
        struct fi_av_attr av_attr = {};

        debug_info("[LFI] Start");

        LFI &lfi = LFI::get_instance();

        std::unique_lock<std::mutex> lock(lfi.m_init_mutex);

        lfi.have_thread = have_threads;

        ret = fi_getinfo(fi_version(), NULL, NULL, 0,
                         fabric_ep.hints, &fabric_ep.info);

        debug_info("[LFI] fi_getinfo = " << ret);
        if (ret)
        {   
            if (ret != -FI_ENODATA){
                printf("fi_getinfo error (%d)\n", ret);
            }
            return ret;
        }

        debug_info("[LFI] " << fi_tostr(fabric_ep.info, FI_TYPE_INFO));

        ret = fi_fabric(fabric_ep.info->fabric_attr, &fabric_ep.fabric, NULL);
        debug_info("[LFI] fi_fabric = " << ret);
        if (ret)
        {
            printf("fi_fabric error (%d)\n", ret);
            return ret;
        }

        ret = fi_domain(fabric_ep.fabric, fabric_ep.info, &fabric_ep.domain, NULL);
        debug_info("[LFI] fi_domain = " << ret);
        if (ret)
        {
            printf("fi_domain error (%d)\n", ret);
            return ret;
        }

        cq_attr.format = FI_CQ_FORMAT_TAGGED;
        cq_attr.wait_obj = FI_WAIT_NONE;
        ret = fi_cq_open(fabric_ep.domain, &cq_attr, &fabric_ep.cq, NULL);
        debug_info("[LFI] fi_cq_open = " << ret);
        if (ret)
        {
            printf("fi_cq_open error (%d)\n", ret);
            return ret;
        }

        av_attr.type = FI_AV_MAP;
        ret = fi_av_open(fabric_ep.domain, &av_attr, &fabric_ep.av, NULL);
        debug_info("[LFI] fi_av_open = " << ret);
        if (ret)
        {
            printf("fi_av_open error (%d)\n", ret);
            return ret;
        }

        // Try opening a scalable endpoint if it is not posible a normal endpoint
        ret = fi_scalable_ep(fabric_ep.domain, fabric_ep.info, &fabric_ep.ep, NULL);
        debug_info("[LFI] fi_scalable_ep = " << ret);
        if (ret == -FI_ENOSYS)
        {
            fabric_ep.use_scalable_ep = false;
        }
        else if (ret)
        {
            printf("fi_scalable_ep error (%d)\n", ret);
            return ret;
        }

        if (fabric_ep.use_scalable_ep)
        {
            ret = fi_scalable_ep_bind(fabric_ep.ep, &fabric_ep.av->fid, 0);
            debug_info("[LFI] fi_scalable_ep_bind = " << ret);
            if (ret)
            {
                printf("fi_scalable_ep_bind av error (%d)\n", ret);
                return ret;
            }

            ret = fi_enable(fabric_ep.ep);
            debug_info("[LFI] fi_enable = " << ret);
            if (ret)
            {
                printf("fi_enable error (%d)\n", ret);
                return ret;
            }

            fabric_ep.info->tx_attr->caps |= FI_MSG;
            fabric_ep.info->tx_attr->caps |= FI_NAMED_RX_CTX; /* Required for scalable endpoints indexing */
            ret = fi_tx_context(fabric_ep.ep, 0, fabric_ep.info->tx_attr, &fabric_ep.tx_ep, NULL);
            debug_info("[LFI] fi_tx_context tx_ep = " << ret);
            if (ret)
            {
                printf("fi_tx_context error (%d)\n", ret);
                return ret;
            }

            ret = fi_ep_bind(fabric_ep.tx_ep, &fabric_ep.cq->fid, FI_SEND);
            debug_info("[LFI] fi_ep_bind tx_ep = " << ret);
            if (ret)
            {
                printf("fi_ep_bind error (%d)\n", ret);
                return ret;
            }

            ret = fi_enable(fabric_ep.tx_ep);
            debug_info("[LFI] fi_enable tx_ep = " << ret);
            if (ret)
            {
                printf("fi_enable error (%d)\n", ret);
                return ret;
            }

            fabric_ep.info->rx_attr->caps |= FI_MSG;
            fabric_ep.info->rx_attr->caps |= FI_NAMED_RX_CTX; /* Required for scalable endpoints indexing */
            ret = fi_rx_context(fabric_ep.ep, 0, fabric_ep.info->rx_attr, &fabric_ep.rx_ep, NULL);
            debug_info("[LFI] fi_rx_context rx_ep = " << ret);
            if (ret)
            {
                printf("fi_rx_context error (%d)\n", ret);
                return ret;
            }

            ret = fi_ep_bind(fabric_ep.rx_ep, &fabric_ep.cq->fid, FI_RECV);
            debug_info("[LFI] fi_ep_bind rx_ep = " << ret);
            if (ret)
            {
                printf("fi_ep_bind error (%d)\n", ret);
                return ret;
            }

            ret = fi_enable(fabric_ep.rx_ep);
            debug_info("[LFI] fi_enable rx_ep = " << ret);
            if (ret)
            {
                printf("fi_enable error (%d)\n", ret);
                return ret;
            }
        }
        else
        {

            ret = fi_endpoint(fabric_ep.domain, fabric_ep.info, &fabric_ep.ep, NULL);
            debug_info("[LFI] fi_endpoint = " << ret);
            if (ret)
            {
                printf("fi_endpoint error (%d)\n", ret);
                return ret;
            }

            ret = fi_ep_bind(fabric_ep.ep, &fabric_ep.av->fid, 0);
            debug_info("[LFI] fi_ep_bind = " << ret);
            if (ret)
            {
                printf("fi_ep_bind error (%d)\n", ret);
                return ret;
            }

            ret = fi_ep_bind(fabric_ep.ep, &fabric_ep.cq->fid, FI_SEND | FI_RECV);
            debug_info("[LFI] fi_ep_bind = " << ret);
            if (ret)
            {
                printf("fi_ep_bind error (%d)\n", ret);
                return ret;
            }

            ret = fi_enable(fabric_ep.ep);
            debug_info("[LFI] fi_enable = " << ret);
            if (ret)
            {
                printf("fi_enable error (%d)\n", ret);
                return ret;
            }
        }

        fabric_ep.enable_ep = true;
        // Create FABRIC_ANY_RANK
        // lfi::any_comm(fabric_ep);

        ret = init_thread_cq();

        debug_info("[LFI] End = " << ret);

        return ret;
    }

    int LFI::destroy(fabric_ep &fabric_ep)
    {
        int ret = 0;

        debug_info("[LFI] Start");
        LFI &lfi = LFI::get_instance();

        std::unique_lock<std::mutex> lock(lfi.m_init_mutex);

        fabric_ep.enable_ep = false;

        if (fabric_ep.tx_ep)
        {
            debug_info("[LFI] Close tx_context");
            ret = fi_close(&fabric_ep.tx_ep->fid);
            if (ret)
                printf("warning: error closing tx_context (%d)\n", ret);
            fabric_ep.tx_ep = nullptr;
        }

        if (fabric_ep.rx_ep)
        {
            debug_info("[LFI] Close rx_context");
            ret = fi_close(&fabric_ep.rx_ep->fid);
            if (ret)
                printf("warning: error closing rx_context (%d)\n", ret);
            fabric_ep.rx_ep = nullptr;
        }

        if (fabric_ep.ep)
        {
            debug_info("[LFI] Close endpoint");
            ret = fi_close(&fabric_ep.ep->fid);
            if (ret)
                printf("warning: error closing EP (%d)\n", ret);
            fabric_ep.ep = nullptr;
        }

        if (fabric_ep.av)
        {
            debug_info("[LFI] Close address vector");
            ret = fi_close(&fabric_ep.av->fid);
            if (ret)
                printf("warning: error closing AV (%d)\n", ret);
            fabric_ep.av = nullptr;
        }

        if (fabric_ep.cq)
        {
            debug_info("[LFI] Close completion queue");
            ret = fi_close(&fabric_ep.cq->fid);
            if (ret)
                printf("warning: error closing CQ (%d)\n", ret);
            fabric_ep.cq = nullptr;
        }

        if (fabric_ep.domain)
        {
            debug_info("[LFI] Close domain");
            ret = fi_close(&fabric_ep.domain->fid);
            if (ret)
                printf("warning: error closing domain (%d)\n", ret);
            fabric_ep.domain = nullptr;
        }

        if (fabric_ep.fabric)
        {
            debug_info("[LFI] Close fabric");
            ret = fi_close(&fabric_ep.fabric->fid);
            if (ret)
                printf("warning: error closing fabric (%d)\n", ret);
            fabric_ep.fabric = nullptr;
        }

        if (fabric_ep.hints)
        {
            debug_info("[LFI] Free hints ");
            fi_freeinfo(fabric_ep.hints);
            fabric_ep.hints = nullptr;
        }

        if (fabric_ep.info)
        {
            debug_info("[LFI] Free info ");
            fi_freeinfo(fabric_ep.info);
            fabric_ep.info = nullptr;
        }

        debug_info("[LFI] End = " << ret);

        return ret;
    }

    fabric_comm &LFI::create_comm(fabric_ep &fabric_ep)
    {
        static uint32_t rank_counter = 0;

        debug_info("[LFI] Start");
        LFI &lfi = LFI::get_instance();
        std::unique_lock<std::mutex> lock(lfi.m_init_mutex);

        auto [key, inserted] = lfi.m_comms.emplace(std::piecewise_construct,
                                                   std::forward_as_tuple(rank_counter),
                                                   std::forward_as_tuple(fabric_ep));
        key->second.rank_peer = rank_counter;
        rank_counter++;
        debug_info("[LFI] rank_peer " << key->second.rank_peer);
        debug_info("[LFI] End");
        return key->second;
    }

    fabric_comm *LFI::get_comm(uint32_t id)
    {
        LFI &lfi = LFI::get_instance();
        if (lfi.m_comms.find(id) == lfi.m_comms.end())
        {
            return nullptr;
        }
        return &lfi.m_comms.at(id);
    }

    int LFI::close_comm(uint32_t id)
    {
        int ret = 0;
        debug_info("[LFI] Start");


        LFI &lfi = LFI::get_instance();
        std::unique_lock<std::mutex> lock(lfi.m_init_mutex);

        fabric_comm *comm = get_comm(id);
        if (comm == nullptr){
            return -1;
        }

        remove_addr(*comm);

        lfi.m_comms.erase(comm->rank_peer);

        debug_info("[LFI] End = " << ret);

        return ret;
    }

    int LFI::get_addr(fabric_comm &fabric_comm, std::vector<uint8_t> &out_addr)
    {
        int ret = -1;
        debug_info("[LFI] Start");

        size_t size_addr = 0;
        ret = fi_getname(&fabric_comm.m_ep.ep->fid, out_addr.data(), &size_addr);
        if (ret != -FI_ETOOSMALL)
        {
            printf("fi_getname error %d\n", ret);
            return ret;
        }
        debug_info("[LFI] size_addr " << size_addr);
        out_addr.resize(size_addr);
        ret = fi_getname(&fabric_comm.m_ep.ep->fid, out_addr.data(), &size_addr);
        if (ret)
        {
            printf("fi_getname error %d\n", ret);
            return ret;
        }
        debug_info("[LFI] End = " << ret);
        return ret;
    }

    int LFI::register_addr(fabric_comm &fabric_comm, std::vector<uint8_t> &addr)
    {
        int ret = -1;
        fi_addr_t fi_addr;
        debug_info("[LFI] Start");
        ret = fi_av_insert(fabric_comm.m_ep.av, addr.data(), 1, &fi_addr, 0, NULL);
        if (ret != 1)
        {
            printf("av insert error %d\n", ret);
            return ret;
        }

        fabric_comm.fi_addr = fi_addr;

        debug_info("[LFI] End = " << ret);
        return ret;
    }

    int LFI::remove_addr(fabric_comm &fabric_comm)
    {
        int ret = -1;
        debug_info("[LFI] Start");
        ret = fi_av_remove(fabric_comm.m_ep.av, &fabric_comm.fi_addr, 1, 0);
        if (ret < 0)
        {
            printf("av remove error %d\n", ret);
            return ret;
        }
        debug_info("[LFI] End = " << ret);
        return ret;
    }

    int LFI::init_server(int socket)
    {
        int ret;
        debug_info("[LFI] Start");
        // First comunicate the identifier
        std::string host_id = ns::get_host_name() + " " + ns::get_host_ip();
        std::string peer_id;

        // Server recv
        size_t peer_id_size = 0;
        ret = socket::recv(socket, &peer_id_size, sizeof(peer_id_size));
        if (ret != sizeof(peer_id_size))
        {
            return -1;
        }
        peer_id.resize(peer_id_size);
        ret = socket::recv(socket, peer_id.data(), peer_id.size());
        if (ret != static_cast<int>(peer_id.size()))
        {
            return -1;
        }

        // Server send
        size_t host_id_size = host_id.size();
        ret = socket::send(socket, &host_id_size, sizeof(host_id_size));
        if (ret != sizeof(host_id_size))
        {
            return -1;
        }
        ret = socket::send(socket, host_id.data(), host_id.size());
        if (ret != static_cast<int>(host_id.size()))
        {
            return -1;
        }

        debug_info("[LFI] host_id " << host_id << " peer_id " << peer_id);

        // Initialize endpoints
        bool is_shm = host_id == peer_id;
        ret = init_endpoints(is_shm);
        if (ret < 0)
        {
            return ret;
        }

        fabric_comm &comm = init_comm(is_shm);
        
        // Exchange ranks
        ret = socket::recv(socket, &comm.rank_self_in_peer, sizeof(comm.rank_self_in_peer));
        if (ret != sizeof(comm.rank_self_in_peer))
        {
            return -1;
        }
        ret = socket::send(socket, &comm.rank_peer, sizeof(comm.rank_peer));
        if (ret != sizeof(comm.rank_peer))
        {
            return -1;
        }

        // Exchange addr
        std::vector<uint8_t> host_addr;
        std::vector<uint8_t> peer_addr;
        size_t peer_addr_size = 0;
        debug_info("[LFI] recv addr");
        ret = socket::recv(socket, &peer_addr_size, sizeof(peer_addr_size));
        if (ret != sizeof(peer_addr_size))
        {
            return -1;
        }
        peer_addr.resize(peer_addr_size);
        ret = socket::recv(socket, peer_addr.data(), peer_addr.size());
        if (ret != static_cast<int>(peer_addr.size()))
        {
            return -1;
        }
        ret = register_addr(comm, peer_addr);
        if (ret < 0)
        {
            return ret;
        }

        ret = get_addr(comm, host_addr);
        if (ret < 0)
        {
            return ret;
        }
        debug_info("[LFI] send addr");
        size_t host_addr_size = host_addr.size();
        ret = socket::send(socket, &host_addr_size, sizeof(host_addr_size));
        if (ret != sizeof(host_addr_size))
        {
            return -1;
        }
        ret = socket::send(socket, host_addr.data(), host_addr.size());
        if (ret != static_cast<int>(host_addr.size()))
        {
            return -1;
        }

        ret = comm.rank_peer;
        debug_info("[LFI] End = " << ret);
        return ret;
    }

    int LFI::init_client(int socket)
    {
        int ret;
        debug_info("[LFI] Start");
        // First comunicate the identifier
        std::string host_id = ns::get_host_name() + " " + ns::get_host_ip();
        std::string peer_id;

        // Client send
        size_t host_id_size = host_id.size();
        ret = socket::send(socket, &host_id_size, sizeof(host_id_size));
        if (ret != sizeof(host_id_size))
        {
            return -1;
        }
        ret = socket::send(socket, host_id.data(), host_id.size());
        if (ret != static_cast<int>(host_id.size()))
        {
            return -1;
        }

        // Client recv
        size_t peer_id_size = 0;
        ret = socket::recv(socket, &peer_id_size, sizeof(peer_id_size));
        if (ret != sizeof(peer_id_size))
        {
            return -1;
        }
        peer_id.resize(peer_id_size);
        ret = socket::recv(socket, peer_id.data(), peer_id.size());
        if (ret != static_cast<int>(peer_id.size()))
        {
            return -1;
        }

        debug_info("[LFI] host_id " << host_id << " peer_id " << peer_id);

        // Initialize endpoints
        bool is_shm = host_id == peer_id;
        ret = init_endpoints(is_shm);
        if (ret < 0)
        {
            return ret;
        }

        fabric_comm &comm = init_comm(is_shm);

        // Exchange ranks
        ret = socket::send(socket, &comm.rank_peer, sizeof(comm.rank_peer));
        if (ret != sizeof(comm.rank_peer))
        {
            return -1;
        }
        ret = socket::recv(socket, &comm.rank_self_in_peer, sizeof(comm.rank_self_in_peer));
        if (ret != sizeof(comm.rank_self_in_peer))
        {
            return -1;
        }

        // Exchange addr
        std::vector<uint8_t> host_addr;
        std::vector<uint8_t> peer_addr;
        size_t peer_addr_size = 0;

        ret = get_addr(comm, host_addr);
        if (ret < 0)
        {
            return ret;
        }
        debug_info("[LFI] send addr");
        size_t host_addr_size = host_addr.size();
        ret = socket::send(socket, &host_addr_size, sizeof(host_addr_size));
        if (ret != sizeof(host_addr_size))
        {
            return -1;
        }
        ret = socket::send(socket, host_addr.data(), host_addr.size());
        if (ret != static_cast<int>(host_addr.size()))
        {
            return -1;
        }

        debug_info("[LFI] recv addr");
        ret = socket::recv(socket, &peer_addr_size, sizeof(peer_addr_size));
        if (ret != sizeof(peer_addr_size))
        {
            return -1;
        }
        peer_addr.resize(peer_addr_size);
        ret = socket::recv(socket, peer_addr.data(), peer_addr.size());
        if (ret != static_cast<int>(peer_addr.size()))
        {
            return -1;
        }
        ret = register_addr(comm, peer_addr);
        if (ret < 0)
        {
            return ret;
        }


        ret = comm.rank_peer;
        debug_info("[LFI] End = " << ret);
        return ret;
    }

    int LFI::init_endpoints(bool is_shm)
    {
        int ret = 0;
        LFI &lfi = LFI::get_instance();
        debug_info("[LFI] Start");
        if (is_shm)
        {
            if (lfi.shm_ep.initialized())
            {
                return 0;
            }
            set_hints(lfi.shm_ep, "shm");
            ret = init(lfi.shm_ep);
            if (ret < 0)
            {
                set_hints(lfi.shm_ep, "sm2");
                ret = init(lfi.shm_ep);
            }
        }
        else
        {
            if (lfi.peer_ep.initialized())
            {
                return 0;
            }
            set_hints(lfi.peer_ep, "");
            ret = init(lfi.peer_ep);
        }
        debug_info("[LFI] End = " << ret);
        return ret;
    }

    fabric_comm &LFI::init_comm(bool is_shm)
    {
        LFI &lfi = LFI::get_instance();
        if (is_shm)
        {
            return create_comm(lfi.shm_ep);
        }
        else
        {
            return create_comm(lfi.peer_ep);
        }
    }
} // namespace LFI