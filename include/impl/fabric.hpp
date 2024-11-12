
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

#pragma once

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_tagged.h>

#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <vector>

namespace LFI
{

    struct fabric_ep;

    struct fabric_context
    {
        // context necesary for fabric interface
        struct fi_context context;
        uint32_t rank;
        struct fi_cq_tagged_entry entry;
    };

    struct fabric_comm
    {
        uint32_t rank_peer;
        uint32_t rank_self_in_peer;

        fi_addr_t fi_addr;

        fabric_ep &m_ep;

        std::mutex comm_mutex;
        std::condition_variable comm_cv;
        std::atomic_bool wait_context = true;
        fabric_context context;

        fabric_comm(fabric_ep &ep) : m_ep(ep) {}
    };

    struct fabric_ep
    {
        bool use_scalable_ep = true;
        struct fi_info *hints = nullptr;
        struct fi_info *info = nullptr;
        struct fid_fabric *fabric = nullptr;
        struct fid_domain *domain = nullptr;
        struct fid_ep *ep = nullptr;
        struct fid_ep *rx_ep = nullptr;
        struct fid_ep *tx_ep = nullptr;
        struct fid_av *av = nullptr;
        struct fid_cq *cq = nullptr;
        std::atomic_bool enable_ep = false;

        bool initialized() { return enable_ep; }
    };

    struct thread_cq
    {
        std::thread id;
        std::mutex thread_cq_mutex;
        std::condition_variable thread_cq_cv;
        bool thread_cq_is_running = true;
    };

    struct fabric_msg
    {
        uint64_t size = 0;
        uint32_t rank_peer = 0;
        uint32_t rank_self_in_peer = 0;
        uint32_t tag = 0;
        int32_t error = 0;
    };
    class fabric
    {
    public:
        constexpr static const uint32_t FABRIC_ANY_RANK = 0xFFFFFFFF;

    private:
        static fabric_comm &any_comm(fabric_ep &fabric_ep);
        static int set_hints(fabric_ep &fabric_ep);
        static int run_thread_cq(fabric_ep &fabric_ep, uint32_t id);
        static int init_thread_cq(fabric_ep &fabric_ep);
        static int destroy_thread_cq(fabric_ep &fabric_ep);

    public:
        static int init(fabric_ep &fabric, bool have_threads = true);

        static int destroy(fabric_ep &fabric_ep);

        static fabric_comm &new_comm(fabric_ep &fabric_ep);
        static fabric_comm &get_any_rank_comm(fabric_ep &fabric_ep);
        static int close(fabric_ep &fabric_ep, fabric_comm &fabric_comm);
        static int get_addr(fabric_ep &fabric_ep, std::vector<uint8_t> &out_addr);
        static int register_addr(fabric_ep &fabric_ep, fabric_comm &fabric_comm, std::vector<uint8_t> &addr);
        static int remove_addr(fabric_ep &fabric_ep, fabric_comm &fabric_comm);
        static void wait(fabric_ep &fabric_ep, fabric_comm &fabric_comm);
        static fabric_msg send(fabric_ep &fabric_ep, fabric_comm &fabric_comm, const void *buffer, size_t size, uint32_t tag);
        static fabric_msg recv(fabric_ep &fabric_ep, fabric_comm &fabric_comm, void *buffer, size_t size, uint32_t tag);

        static std::mutex s_mutex;
    };

    class LFI
    {
        // Constants
        constexpr static const uint32_t FABRIC_ANY_RANK = 0xFFFFFFFF;

        // Asegurate destroy when close app
        // fabric_init
    public:
        ~LFI();

    private:
        static int set_hints(fabric_ep &fabric_ep, const std::string &prov);
        static int init(fabric_ep &fabric, bool have_threads = true);
        static int destroy(fabric_ep &fabric_ep);
        static fabric_comm &create_comm(fabric_ep &fabric_ep);

    public:
        static fabric_comm *get_comm(uint32_t id);
        static int close_comm(uint32_t id);
        static int get_addr(fabric_comm &fabric_comm, std::vector<uint8_t> &out_addr);
        static int register_addr(fabric_comm &fabric_comm, std::vector<uint8_t> &addr);
        static int remove_addr(fabric_comm &fabric_comm);

        static int init_server(int socket);
        static int init_client(int socket);

        static int init_endpoints(bool is_shm);
        static fabric_comm &init_comm(bool is_shm);

        // fabric_thread
    private:
        static int init_thread_cq();
        static int destroy_thread_cq();
        static int progress(fabric_ep &fabric_ep);
        static int run_thread_cq(uint32_t id);

        // fabric_send_recv
    public:
        static void wait(fabric_comm *fabric_comm);
        static fabric_msg send(uint32_t comm_id, const void *buffer, size_t size, uint32_t tag);
        static fabric_msg recv(uint32_t comm_id, void *buffer, size_t size, uint32_t tag);

        // Variables
    public:
        fabric_ep shm_ep;
        fabric_ep peer_ep;

        std::unordered_map<uint32_t, fabric_comm> m_comms;

        std::mutex m_init_mutex;

        // Thread
        bool have_thread = true;
        std::vector<thread_cq> threads_cq;

        std::atomic_uint32_t subs_to_wait = 0;

    public:
        static inline LFI &get_instance()
        {
            static LFI instance;
            return instance;
        }
    };

} // namespace LFI