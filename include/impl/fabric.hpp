
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
#include <unordered_set>
#include <thread>
#include <atomic>
#include <vector>
#include <sstream>

#define DECLARE_LFI_ERROR(name, num, msg) \
    static constexpr const int name = num; \
    static constexpr const char* name##_str = msg;

#define CASE_STR_ERROR(name) \
    case name: return name##_str;

namespace LFI
{
    // Error codes
    DECLARE_LFI_ERROR(LFI_SUCCESS,          0, "Success");
    DECLARE_LFI_ERROR(LFI_ERROR,            1, "Error");
    DECLARE_LFI_ERROR(LFI_TIMEOUT,          2, "Timeout");
    DECLARE_LFI_ERROR(LFI_CANCELED,         3, "Canceled");
    DECLARE_LFI_ERROR(LFI_CANCELED_COMM,    4, "Canceled COMM");
    DECLARE_LFI_ERROR(LFI_COMM_NOT_FOUND,   5, "COMM not found");

    static constexpr const char * lfi_strerror(int error)
    {
        switch (error)
        {
            CASE_STR_ERROR(LFI_SUCCESS);
            CASE_STR_ERROR(LFI_ERROR);
            CASE_STR_ERROR(LFI_TIMEOUT);
            CASE_STR_ERROR(LFI_CANCELED);
            CASE_STR_ERROR(LFI_CANCELED_COMM);
            CASE_STR_ERROR(LFI_COMM_NOT_FOUND);
        default: return "Unknown";
        }
    }

    // Reserved tags
    #define LFI_TAG_FT 65535
    #define LFI_TAG_RECV_LD_PRELOAD 65534

    // Forward declaration
    struct fabric_ep;
    struct fabric_comm;

    struct fabric_request
    {
        // context necesary for fabric interface
        struct fi_context context;

        fabric_comm& m_comm;
        std::mutex mutex;
        std::condition_variable cv;
        bool wait_context = true;
        int error = 0;

        bool is_send;
        bool is_inject = false;
        
        fi_cq_tagged_entry entry = {};
        fabric_request() = delete;
        fabric_request(fabric_comm &comm) : m_comm(comm) {}
        fabric_request(const fabric_request &&request) : m_comm(request.m_comm) {}

        void reset()
        {
            wait_context = true;
            error = 0;
        }

        std::string to_string()
        {
            std::stringstream out;
            out << "Request "<<std::hex<<this;
            if (is_send){ out << " is_send "; }
            if (is_inject){ out << "is_inject"; }
            return out.str();
        }

        static fabric_request Create(uint32_t comm_id);
    };

    struct fabric_comm
    {
        uint32_t rank_peer;
        uint32_t rank_self_in_peer;

        fi_addr_t fi_addr = FI_ADDR_UNSPEC;

        fabric_ep &m_ep;

        // For fault tolerance
        std::mutex ft_mutex;
        std::unordered_set<fabric_request*> ft_requests;
        bool ft_error = false;

        bool is_canceled = false;

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
        bool is_shm = false;

        std::mutex mutex_ep = {};
        std::mutex mutex_send_recv = {};

        bool initialized() { return enable_ep; }
    };

    struct fabric_msg
    {
        uint64_t size = 0;
        uint32_t rank_peer = 0;
        uint32_t rank_self_in_peer = 0;
        uint32_t tag = 0;
        int32_t error = 0;
    };

    class LFI
    {
        // Constants
        constexpr static const uint32_t LFI_ANY_COMM = 0xFFFFFF;
        constexpr static const uint32_t LFI_ANY_COMM_PEER = LFI_ANY_COMM - 1;
        constexpr static const uint32_t LFI_ANY_COMM_SHM = LFI_ANY_COMM - 2;

        // Secure destroy when closing app
        // fabric_init
    public:
        ~LFI();

    private:
        static int set_hints(fabric_ep &fabric_ep, const std::string &prov);
        static int init(fabric_ep &fabric);
        static int destroy(fabric_ep &fabric_ep);
        static fabric_comm &create_comm(fabric_ep &fabric_ep, int32_t comm_id = -1);
        static fabric_comm &create_any_comm(fabric_ep &fabric_ep, uint32_t comm_id);

    public:
        static fabric_comm *get_comm(uint32_t id);
        static int close_comm(uint32_t id);
        static int get_addr(fabric_comm &fabric_comm, std::vector<uint8_t> &out_addr);
        static int register_addr(fabric_comm &fabric_comm, std::vector<uint8_t> &addr);
        static int remove_addr(fabric_comm &fabric_comm);

        static int init_server(int socket, int32_t comm_id = -1);
        static int init_client(int socket, int32_t comm_id = -1);

        static int init_endpoints();
        static fabric_comm &init_comm(bool is_shm, int32_t comm_id = -1);

        // fabric_send_recv
    private:
        static inline bool wait_check_timeout(int32_t timeout_ms, decltype(std::chrono::high_resolution_clock::now()) start);
    public:
        static int progress(fabric_request &request);
        static int wait(fabric_request &request, int32_t timeout_ms = -1);
        static int cancel(fabric_request &request);
        static fabric_msg async_send(const void *buffer, size_t size, uint32_t tag, fabric_request &request, int32_t timeout_ms = -1);
        static fabric_msg async_recv(void *buffer, size_t size, uint32_t tag, fabric_request &request, int32_t timeout_ms = -1);
        static fabric_msg send(uint32_t comm_id, const void *buffer, size_t size, uint32_t tag);
        static fabric_msg recv(uint32_t comm_id, void *buffer, size_t size, uint32_t tag);
        static fabric_msg any_recv(void *buffer, size_t size, uint32_t tag);

        // fabric_ft for fault tolerance
    public:
        static int ft_thread_start();
        static int ft_thread_loop();
        static int ft_thread_destroy();

        // Variables
    public:
        fabric_ep shm_ep = {.is_shm = true};
        fabric_ep peer_ep = {.is_shm = false};

        std::mutex m_mutex;
        std::unordered_map<uint32_t, fabric_comm> m_comms;

        // Fault tolerance
        std::thread ft_thread;
        std::mutex ft_mutex;
        std::condition_variable ft_cv;
        bool ft_is_running = false;

    public:
        static inline LFI &get_instance()
        {
            static LFI instance;
            return instance;
        }
    };

} // namespace LFI