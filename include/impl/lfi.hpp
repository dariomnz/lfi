
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

#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "impl/ft_manager.hpp"
#include "lfi_comm.hpp"
#include "lfi_endpoint.hpp"
#include "lfi_error.h"
#include "lfi_request.hpp"
#include "lfi_request_context.hpp"

#define CASE_STR_ERROR(name, msg) \
    case name:                    \
        return msg;

namespace LFI {
// Error codes

static constexpr const char *lfi_strerror(int error) {
    switch (std::abs(error)) {
        CASE_STR_ERROR(LFI_SUCCESS, "Success");
        CASE_STR_ERROR(LFI_ERROR, "General error");
        CASE_STR_ERROR(LFI_TIMEOUT, "Timeout");
        CASE_STR_ERROR(LFI_CANCELED, "Canceled");
        CASE_STR_ERROR(LFI_BROKEN_COMM, "Broken comunicator");
        CASE_STR_ERROR(LFI_COMM_NOT_FOUND, "Comunicator not found");
        CASE_STR_ERROR(LFI_PEEK_NO_MSG, "No msg encounter");
        CASE_STR_ERROR(LFI_NOT_COMPLETED, "Request not completed");
        CASE_STR_ERROR(LFI_NULL_REQUEST, "Request is NULL");
        CASE_STR_ERROR(LFI_SEND_ANY_COMM, "Use of ANY_COMM in send");
        CASE_STR_ERROR(LFI_ETRUN_RECV, "The published receive buffer is smaller than the received one.");
        CASE_STR_ERROR(LFI_LIBFABRIC_ERROR, "Internal libfabric error");
        CASE_STR_ERROR(LFI_GROUP_NO_INIT, "The group is not initialized");
        CASE_STR_ERROR(LFI_GROUP_NO_SELF, "The hostname of the current process is missing");
        CASE_STR_ERROR(LFI_GROUP_INVAL, "Invalid argument");
        default:
            return "Unknown";
    }
}

// Reserved tags
#define LFI_TAG_FT_PING             (0xFFFFFFFF - 1)
#define LFI_TAG_FT_PONG             (0xFFFFFFFF - 2)
#define LFI_TAG_RECV_LD_PRELOAD     (0xFFFFFFFF - 3)
#define LFI_TAG_BUFFERED_LD_PRELOAD 100000
#define LFI_TAG_GROUP               (0xFFFFFFFF - 4)
#define LFI_TAG_BARRIER             (0xFFFFFFFF - 5)
#define LFI_TAG_BROADCAST           (0xFFFFFFFF - 6)
#define LFI_TAG_ALLREDUCE           (0xFFFFFFFF - 7)
#define LFI_TAG_DUMMY               (0xFFFFFFFF - 8)
#define LFI_TAG_INITIAL_SEND_SRV    (0xFFFFFFFF - 9)
#define LFI_TAG_INITIAL_SEND_CLI    (0xFFFFFFFF - 10)

// Constants
constexpr static const uint64_t MASK_RANK = 0xFFFF'FFFF'0000'0000;
constexpr static const uint64_t MASK_RANK_BYTES = 32;
constexpr static const uint64_t MASK_TAG = 0x0000'0000'FFFF'FFFF;
constexpr static const uint64_t MASK_TAG_BYTES = 32;

class LFI {
    // address.cpp
   public:
    int get_addr(lfi_comm &lfi_comm, std::vector<uint8_t> &out_addr);
    int register_addr(lfi_comm &lfi_comm, std::vector<uint8_t> &addr);
    int remove_addr(lfi_comm &lfi_comm);

    // comm.cpp
   public:
    uint32_t reserve_comm();
    lfi_comm *init_comm(bool is_shm, int32_t comm_id);
    lfi_comm *get_comm(uint32_t id);
    std::pair<std::shared_lock<std::shared_mutex>, lfi_comm *> get_comm_and_mutex(uint32_t id);
    lfi_comm *get_comm_internal(std::shared_lock<std::shared_mutex> &comms_lock, uint32_t id);
    int close_comm(uint32_t id);

   private:
    lfi_comm *init_comm(lfi_endpoint &lfi_ep, int32_t comm_id);
    lfi_comm *create_any_comm(lfi_endpoint &lfi_ep, uint32_t comm_id);

    // connection.cpp
   public:
    int init_server(int socket, int32_t comm_id);
    int init_client(int socket, int32_t comm_id);

    // init.cpp
   public:
    LFI();
    // Secure close ep when closing app
    ~LFI();

   private:
    int set_hints(lfi_endpoint &lfi_ep, const std::string &prov);
    int init(lfi_endpoint &lfi_ep);
    int destroy(lfi_endpoint &lfi_ep);

    // recv.cpp
   public:
    enum class recv_type {
        RECV,
        RECVV,
    };
    lfi_msg recv_internal(uint32_t comm_id, void *ptr, size_t size, recv_type type, uint32_t tag);
    int async_recv_internal(void *buffer, size_t size, recv_type type, uint32_t tag, lfi_request &request,
                            bool priority = false);
    // Redirects
    lfi_msg recv(uint32_t comm_id, void *buffer, size_t size, uint32_t tag) {
        return recv_internal(comm_id, buffer, size, recv_type::RECV, tag);
    }
    lfi_msg recvv(uint32_t comm_id, struct iovec *iov, size_t count, uint32_t tag) {
        return recv_internal(comm_id, reinterpret_cast<void *>(iov), count, recv_type::RECVV, tag);
    }
    int async_recv(void *buffer, size_t size, uint32_t tag, lfi_request &request, bool priority = false) {
        return async_recv_internal(buffer, size, recv_type::RECV, tag, request, priority);
    }
    int async_recvv(struct iovec *iov, size_t count, uint32_t tag, lfi_request &request, bool priority = false) {
        return async_recv_internal(reinterpret_cast<void *>(iov), count, recv_type::RECVV, tag, request, priority);
    }

    // send.cpp
   public:
    enum class send_type {
        SEND,
        SENDV,
    };
    lfi_msg send_internal(uint32_t comm_id, const void *ptr, size_t size, send_type type, uint32_t tag);
    int async_send_internal(const void *buffer, size_t size, send_type type, uint32_t tag, lfi_request &request,
                            bool priority = false);
    // Redirects
    lfi_msg send(uint32_t comm_id, const void *buffer, size_t size, uint32_t tag) {
        return send_internal(comm_id, buffer, size, send_type::SEND, tag);
    }
    lfi_msg sendv(uint32_t comm_id, const struct iovec *iov, size_t count, uint32_t tag) {
        return send_internal(comm_id, reinterpret_cast<const void *>(iov), count, send_type::SENDV, tag);
    }
    int async_send(const void *buffer, size_t size, uint32_t tag, lfi_request &request, bool priority = false) {
        return async_send_internal(buffer, size, send_type::SEND, tag, request, priority);
    }
    int async_sendv(const struct iovec *iov, size_t count, uint32_t tag, lfi_request &request, bool priority = false) {
        return async_send_internal(reinterpret_cast<const void *>(iov), count, send_type::SENDV, tag, request,
                                   priority);
    }
    // wait.cpp
   public:
    inline bool wait_check_timeout(int32_t timeout_ms, decltype(std::chrono::high_resolution_clock::now()) start);

    int wait(lfi_request &request, int32_t timeout_ms = -1);
    int wait_num(lfi_request **request, int n_request, int how_many, int32_t timeout_ms = -1);
    int test(lfi_request &request);
    int test_num(lfi_request **request, int n_request, int how_many);

   public:
    void dump_stats();
    // Variables
   public:
    lfi_endpoint shm_ep{*this, true};
    lfi_endpoint peer_ep{*this, false};

    lfi_ft_manager m_ft_manager;

    std::shared_mutex m_comms_mutex;
    std::mutex m_fut_comms_mutex;
    std::condition_variable_any m_fut_wait_cv;
    std::unordered_map<uint32_t, std::future<uint32_t>> m_fut_comms;
    std::unordered_map<uint32_t, std::unique_ptr<lfi_comm>> m_comms;
    std::atomic_uint32_t m_rank_counter = {0};

    lfi_request_context_factory req_ctx_factory;

   public:
    static inline LFI &get_instance() {
        static LFI instance;
        return instance;
    }
};

}  // namespace LFI