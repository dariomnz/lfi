
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
#include "impl/debug.hpp"

#include "sstream"

namespace LFI
{
    static inline std::string fi_flags_to_string(uint64_t flags)
    {
        std::stringstream out;
        if (flags & FI_MSG) { out << "    FI_MSG" << std::endl; }
        if (flags & FI_RMA) { out << "    FI_RMA" << std::endl; }
        if (flags & FI_TAGGED) { out << "    FI_TAGGED" << std::endl; }
        if (flags & FI_ATOMIC) { out << "    FI_ATOMIC" << std::endl; }
        if (flags & FI_MULTICAST) { out << "    FI_MULTICAST" << std::endl; }
        if (flags & FI_COLLECTIVE) { out << "    FI_COLLECTIVE" << std::endl; }

        if (flags & FI_READ) { out << "    FI_READ" << std::endl; }
        if (flags & FI_WRITE) { out << "    FI_WRITE" << std::endl; }
        if (flags & FI_RECV) { out << "    FI_RECV" << std::endl; }
        if (flags & FI_SEND) { out << "    FI_SEND" << std::endl; }
        if (flags & FI_REMOTE_READ) { out << "    FI_REMOTE_READ" << std::endl; }
        if (flags & FI_REMOTE_WRITE) { out << "    FI_REMOTE_WRITE" << std::endl; }

        if (flags & FI_MULTI_RECV) { out << "    FI_MULTI_RECV" << std::endl; }
        if (flags & FI_REMOTE_CQ_DATA) { out << "    FI_REMOTE_CQ_DATA" << std::endl; }
        if (flags & FI_MORE) { out << "    FI_MORE" << std::endl; }
        if (flags & FI_PEEK) { out << "    FI_PEEK" << std::endl; }
        if (flags & FI_TRIGGER) { out << "    FI_TRIGGER" << std::endl; }
        if (flags & FI_FENCE) { out << "    FI_FENCE" << std::endl; }
        // if (flags & FI_PRIORITY) { out << "    FI_PRIORITY" << std::endl; }

        if (flags & FI_COMPLETION) { out << "    FI_COMPLETION" << std::endl; }
        if (flags & FI_INJECT) { out << "    FI_INJECT" << std::endl; }
        if (flags & FI_INJECT_COMPLETE) { out << "    FI_INJECT_COMPLETE" << std::endl; }
        if (flags & FI_TRANSMIT_COMPLETE) { out << "    FI_TRANSMIT_COMPLETE" << std::endl; }
        if (flags & FI_DELIVERY_COMPLETE) { out << "    FI_DELIVERY_COMPLETE" << std::endl; }
        if (flags & FI_AFFINITY) { out << "    FI_AFFINITY" << std::endl; }
        if (flags & FI_COMMIT_COMPLETE) { out << "    FI_COMMIT_COMPLETE" << std::endl; }
        if (flags & FI_MATCH_COMPLETE) { out << "    FI_MATCH_COMPLETE" << std::endl; }

        if (flags & FI_HMEM) { out << "    FI_HMEM" << std::endl; }
        if (flags & FI_VARIABLE_MSG) { out << "    FI_VARIABLE_MSG" << std::endl; }
        if (flags & FI_RMA_PMEM) { out << "    FI_RMA_PMEM" << std::endl; }
        if (flags & FI_SOURCE_ERR) { out << "    FI_SOURCE_ERR" << std::endl; }
        if (flags & FI_LOCAL_COMM) { out << "    FI_LOCAL_COMM" << std::endl; }
        if (flags & FI_REMOTE_COMM) { out << "    FI_REMOTE_COMM" << std::endl; }
        if (flags & FI_SHARED_AV) { out << "    FI_SHARED_AV" << std::endl; }
        if (flags & FI_PROV_ATTR_ONLY) { out << "    FI_PROV_ATTR_ONLY" << std::endl; }
        if (flags & FI_NUMERICHOST) { out << "    FI_NUMERICHOST" << std::endl; }
        if (flags & FI_RMA_EVENT) { out << "    FI_RMA_EVENT" << std::endl; }
        if (flags & FI_SOURCE) { out << "    FI_SOURCE" << std::endl; }
        if (flags & FI_NAMED_RX_CTX) { out << "    FI_NAMED_RX_CTX" << std::endl; }
        if (flags & FI_DIRECTED_RECV) { out << "    FI_DIRECTED_RECV" << std::endl; }
        return out.str();
    }

    static inline std::string fi_cq_tagged_entry_to_string(const fi_cq_tagged_entry &entry)
    {
        std::stringstream out;
        out << "fi_cq_tagged_entry:" << std::endl;
        out << "  op_context: " << entry.op_context << std::endl;
        out << "  Flags set:" << std::endl;
        out << fi_flags_to_string(entry.flags);
        out << "  len: " << entry.len << std::endl;
        out << "  buf: " << entry.buf << std::endl;
        out << "  data: " << entry.data << std::endl;
        out << "  tag: " << entry.tag << std::endl;
        return out.str();
    }

    static inline std::string fi_cq_err_entry_to_string(const fi_cq_err_entry &entry)
    {
        std::stringstream out;
        out << "fi_cq_err_entry:" << std::endl;
        out << "  op_context: " << entry.op_context << std::endl;
        out << "  Flags set:" << std::endl;
        out << fi_flags_to_string(entry.flags);
        out << "  len: " << entry.len << std::endl;
        out << "  buf: " << entry.buf << std::endl;
        out << "  data: " << entry.data << std::endl;
        out << "  tag: " << entry.tag << std::endl;
        out << "  olen: " << entry.olen << std::endl;
        out << "  err: " << entry.err << " " << fi_strerror(entry.err) << std::endl;
        out << "  prov_errno: " << entry.prov_errno << " " << fi_strerror(entry.err) << std::endl;
        out << "  err_data: " << entry.err_data << std::endl;
        out << "  err_data_size: " << entry.err_data_size << std::endl;
        return out.str();
    }

    fabric_request fabric_request::Create(uint32_t comm_id) {
        LFI& lfi = LFI::get_instance();
        fabric_comm *comm = lfi.get_comm(comm_id);
        if (comm == nullptr){
            throw std::runtime_error("fabric_comm not found");
        }
        return fabric_request{*comm};
    }

    int LFI::progress(fabric_ep &fabric_ep)
    {
        int ret;
        const int comp_count = 8;
        struct fi_cq_tagged_entry comp[comp_count] = {};
        // sizeof(comp)
        
        // Libfabric progress
        ret = fi_cq_read(fabric_ep.cq, comp, comp_count);
        if (ret == -FI_EAGAIN)
        {
            return 0;
        }

        // TODO: handle error
        if (ret < 0)
        {
            print("[Error] fi_cq_read "<<ret<<" "<<fi_strerror(ret));
            fi_cq_err_entry err;
            fi_cq_readerr(fabric_ep.cq, &err, 0);
            print(fi_cq_err_entry_to_string(err));
            return ret;
        }

        // Handle the cq entries
        for (int i = 0; i < ret; i++)
        {
            fabric_request *request = static_cast<fabric_request *>(comp[i].op_context);
            request->entry = comp[i];

            debug_info(fi_cq_tagged_entry_to_string(comp[i]));
            std::unique_lock lock(request->mutex);
            request->wait_context = false;
            request->cv.notify_one();
        }
        // print("lfi process "<<ret<<" num entrys");
        return ret;
    }

    // The pointer must not be nullptr
    void LFI::wait(fabric_request &request)
    {
        debug_info("[LFI] Start Without threads");
        std::unique_lock global_lock(request.m_comm.m_ep.mutex_ep, std::defer_lock);
        std::unique_lock request_lock(request.mutex);

        while (request.wait_context)
        {
            if (global_lock.try_lock()){
                while (request.wait_context)
                {
                    progress(request.m_comm.m_ep);
                }
                global_lock.unlock();
            }else{
                if (request.wait_context){
                    request.cv.wait_for(request_lock, std::chrono::milliseconds(10));
                }
            }
        }
        debug_info("[LFI] End Without threads");
    }

    fabric_msg LFI::send(uint32_t comm_id, const void *buffer, size_t size, uint32_t tag)
    {
        int ret;
        fabric_msg msg = {};

        // Check if comm exists
        fabric_comm *comm = get_comm(comm_id);
        if (comm == nullptr){
            msg.error = -1;
            return msg;
        }
        fabric_request request(*comm);

        // tag format 24 bits rank_peer 24 bits rank_self_in_peer 16 bits tag
        uint64_t aux_rank_peer = comm->rank_peer;
        uint64_t aux_rank_self_in_peer = comm->rank_self_in_peer;
        uint64_t aux_tag = tag;
        uint64_t tag_send = (aux_rank_peer << 40) | (aux_rank_self_in_peer << 16) | aux_tag;

        debug_info("[LFI] Start size " << size << " rank_peer " << comm->rank_peer << " rank_self_in_peer " << comm->rank_self_in_peer << " tag " << tag << " send_context " << (void *)&request.context);

        if (size > comm->m_ep.info->tx_attr->inject_size)
        {
            fid_ep *p_tx_ep = comm->m_ep.use_scalable_ep ? comm->m_ep.tx_ep : comm->m_ep.ep;
            do
            {
                ret = fi_tsend(p_tx_ep, buffer, size, NULL, comm->fi_addr, tag_send, &request.context);

                if (ret == -FI_EAGAIN){
                    std::unique_lock global_lock(comm->m_ep.mutex_ep, std::defer_lock);
                    if (global_lock.try_lock()){
                        progress(comm->m_ep);
                        global_lock.unlock();
                    }
                }
            } while (ret == -FI_EAGAIN);

            if (ret)
            {
                printf("error posting send buffer (%d)\n", ret);
                msg.error = -1;
                return msg;
            }

            debug_info("[LFI] Waiting on rank_peer " << comm->rank_peer);

            wait(request);
        }
        else
        {
            fid_ep *p_tx_ep = comm->m_ep.use_scalable_ep ? comm->m_ep.tx_ep : comm->m_ep.ep;
            do
            {
                ret = fi_tinject(p_tx_ep, buffer, size, comm->fi_addr, tag_send);

                if (ret == -FI_EAGAIN){
                    std::unique_lock global_lock(comm->m_ep.mutex_ep, std::defer_lock);
                    if (global_lock.try_lock()){
                        progress(comm->m_ep);
                        global_lock.unlock();
                    }
                }
            } while (ret == -FI_EAGAIN);
            debug_info("[LFI] fi_tinject of " << size << " for rank_peer " << comm->rank_peer);
        }

        msg.size = size;

        msg.tag = tag_send & 0x0000'0000'0000'FFFF;
        msg.rank_peer = (tag_send & 0xFFFF'FF00'0000'0000) >> 40;
        msg.rank_self_in_peer = (tag_send & 0x0000'00FF'FFFF'0000) >> 16;

        debug_info("[LFI] msg size " << msg.size << " rank_peer " << msg.rank_peer << " rank_self_in_peer " << msg.rank_self_in_peer << " tag " << msg.tag << " error " << msg.error);
        debug_info("[LFI] End = " << size);
        return msg;
    }

    fabric_msg LFI::recv(uint32_t comm_id, void *buffer, size_t size, uint32_t tag)
    {
        int ret;
        fabric_msg msg = {};

        // Check if comm exists
        fabric_comm *comm = get_comm(comm_id);
        if (comm == nullptr){
            msg.error = -1;
            return msg;
        }
        fabric_request request(*comm);

        uint64_t mask = 0;
        // tag format 24 bits rank_self_in_peer 24 bits rank_peer 16 bits tag
        uint64_t aux_rank_peer = comm->rank_peer;
        uint64_t aux_rank_self_in_peer = comm->rank_self_in_peer;
        uint64_t aux_tag = tag;
        uint64_t tag_recv = (aux_rank_self_in_peer << 40) | (aux_rank_peer << 16) | aux_tag;

        if (comm->rank_peer == FABRIC_ANY_RANK)
        {
            // mask = 0x0000'00FF'FFFF'0000;
            // mask = 0xFFFF'FF00'0000'0000;
            mask = 0xFFFF'FFFF'FFFF'0000;
        }

        debug_info("[LFI] Start size " << size << " rank_peer " << comm->rank_peer << " rank_self_in_peer " << comm->rank_self_in_peer << " tag " << tag << " recv_context " << (void *)&request.context);

        fid_ep *p_rx_ep = comm->m_ep.use_scalable_ep ? comm->m_ep.rx_ep : comm->m_ep.ep;
        do
        {
            ret = fi_trecv(p_rx_ep, buffer, size, NULL, comm->fi_addr, tag_recv, mask, &request.context);

            if (ret == -FI_EAGAIN){
                std::unique_lock global_lock(comm->m_ep.mutex_ep, std::defer_lock);
                if (global_lock.try_lock()){
                    progress(comm->m_ep);
                    global_lock.unlock();
                }
            }
        } while (ret == -FI_EAGAIN);

        if (ret)
        {
            printf("error posting recv buffer (%d)\n", ret);
            msg.error = -1;
            return msg;
        }

        debug_info("[LFI] Waiting on rank_peer " << comm->rank_peer);

        wait(request);

        msg.size = size;
        // msg.error = request.context.entry.err;

        msg.tag = request.entry.tag & 0x0000'0000'0000'FFFF;
        msg.rank_self_in_peer = (request.entry.tag & 0xFFFF'FF00'0000'0000) >> 40;
        msg.rank_peer = (request.entry.tag & 0x0000'00FF'FFFF'0000) >> 16;

        debug_info("[LFI] msg size " << msg.size << " rank_peer " << msg.rank_peer << " rank_self_in_peer " << msg.rank_self_in_peer << " tag " << msg.tag << " error " << msg.error);
        debug_info("[LFI] End = " << size);
        return msg;
    }
} // namespace LFI