
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
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "impl/env.hpp"
#include "lfi.h"
#include "lfi_async.h"
#include "lfi_comm.hpp"
#include "lfi_endpoint.hpp"
#include "lfi_error.h"
#include "lfi_request.hpp"

#define DECLARE_LFI_ERROR(name, msg) static constexpr const char *name##_str = msg;

#define CASE_STR_ERROR(name) \
    case name:               \
        return name##_str;

namespace LFI {
// Error codes
DECLARE_LFI_ERROR(LFI_SUCCESS, "Success");
DECLARE_LFI_ERROR(LFI_ERROR, "General error");
DECLARE_LFI_ERROR(LFI_TIMEOUT, "Timeout");
DECLARE_LFI_ERROR(LFI_CANCELED, "Canceled");
DECLARE_LFI_ERROR(LFI_BROKEN_COMM, "Broken comunicator");
DECLARE_LFI_ERROR(LFI_COMM_NOT_FOUND, "Comunicator not found");
DECLARE_LFI_ERROR(LFI_PEEK_NO_MSG, "No msg encounter");
DECLARE_LFI_ERROR(LFI_NOT_COMPLETED, "Request not completed");
DECLARE_LFI_ERROR(LFI_NULL_REQUEST, "Request is NULL");
DECLARE_LFI_ERROR(LFI_SEND_ANY_COMM, "Use of ANY_COMM in send");
DECLARE_LFI_ERROR(LFI_LIBFABRIC_ERROR, "Internal libfabric error");
DECLARE_LFI_ERROR(LFI_GROUP_NO_INIT, "The group is not initialized");
DECLARE_LFI_ERROR(LFI_GROUP_NO_SELF, "The hostname of the current process is missing");
DECLARE_LFI_ERROR(LFI_GROUP_INVAL, "Invalid argument");

static constexpr const char *lfi_strerror(int error) {
    switch (std::abs(error)) {
        CASE_STR_ERROR(LFI_SUCCESS);
        CASE_STR_ERROR(LFI_ERROR);
        CASE_STR_ERROR(LFI_TIMEOUT);
        CASE_STR_ERROR(LFI_CANCELED);
        CASE_STR_ERROR(LFI_BROKEN_COMM);
        CASE_STR_ERROR(LFI_COMM_NOT_FOUND);
        CASE_STR_ERROR(LFI_PEEK_NO_MSG);
        CASE_STR_ERROR(LFI_NOT_COMPLETED);
        CASE_STR_ERROR(LFI_NULL_REQUEST);
        CASE_STR_ERROR(LFI_SEND_ANY_COMM);
        CASE_STR_ERROR(LFI_LIBFABRIC_ERROR);
        CASE_STR_ERROR(LFI_GROUP_NO_INIT);
        CASE_STR_ERROR(LFI_GROUP_NO_SELF);
        CASE_STR_ERROR(LFI_GROUP_INVAL);
        default:
            return "Unknown";
    }
}

// Reserved tags
#define LFI_TAG_FT_PING             (0xFFFFFFFF - 1)
#define LFI_TAG_FT_PONG             (0xFFFFFFFF - 2)
#define LFI_TAG_RECV_LD_PRELOAD     (0xFFFFFFFF - 3)
#define LFI_TAG_BUFFERED_LD_PRELOAD 100000
#define LFI_TAG_BARRIER             (0xFFFFFFFF - 4)
#define LFI_TAG_BROADCAST           (0xFFFFFFFF - 5)
#define LFI_TAG_ALLREDUCE           (0xFFFFFFFF - 6)
#define LFI_TAG_DUMMY               (0xFFFFFFFF - 7)
#define LFI_TAG_INITIAL_SEND_SRV    (0xFFFFFFFF - 8)
#define LFI_TAG_INITIAL_SEND_CLI    (0xFFFFFFFF - 9)

// Constants
constexpr static const uint64_t MASK_RANK = 0xFFFF'FFFF'0000'0000;
constexpr static const uint64_t MASK_RANK_BYTES = 32;
constexpr static const uint64_t MASK_TAG = 0x0000'0000'FFFF'FFFF;
constexpr static const uint64_t MASK_TAG_BYTES = 32;

static inline std::string lfi_tag_to_string(int64_t tag) {
    switch (tag) {
        case LFI_TAG_FT_PING:
            return "FT_PING";
        case LFI_TAG_FT_PONG:
            return "FT_PONG";
        case LFI_TAG_RECV_LD_PRELOAD:
            return "RECV_LD_PRELOAD";
        case LFI_TAG_BUFFERED_LD_PRELOAD:
            return "BUFFERED_LD_PRELOAD";
        case LFI_TAG_BARRIER:
            return "BARRIER";
        case LFI_TAG_BROADCAST:
            return "BROADCAST";
        case LFI_TAG_ALLREDUCE:
            return "ALLREDUCE";
        case LFI_TAG_DUMMY:
            return "DUMMY";
        case LFI_TAG_INITIAL_SEND_SRV:
            return "INITIAL_SEND_SRV";
        case LFI_TAG_INITIAL_SEND_CLI:
            return "INITIAL_SEND_CLI";
        default:
            return std::to_string(tag);
    }
}

static inline std::string lfi_comm_to_string(int64_t source) {
    switch (source) {
        case LFI_ANY_COMM_SHM:
            return "ANY_SHM";
        case LFI_ANY_COMM_PEER:
            return "ANY_PEER";
        case UNINITIALIZED_COMM:
            return "UNINITIALIZED_COMM";
        default:
            return std::to_string(source);
    }
}

class LFI {
    // address.cpp
   public:
    int get_addr(lfi_comm &lfi_comm, std::vector<uint8_t> &out_addr);
    int register_addr(lfi_comm &lfi_comm, std::vector<uint8_t> &addr);
    int remove_addr(lfi_comm &lfi_comm);

    // comm.cpp
   public:
    uint32_t reserve_comm();
    lfi_comm *init_comm(bool is_shm, int32_t comm_id = -1);
    lfi_comm *get_comm(uint32_t id);
    int close_comm(uint32_t id);

   private:
    lfi_comm *create_comm(lfi_endpoint &lfi_ep, int32_t comm_id = -1);
    lfi_comm *create_any_comm(lfi_endpoint &lfi_ep, uint32_t comm_id);

    // connection.cpp
   public:
    int init_server(int socket, int32_t comm_id = -1);
    int init_client(int socket, int32_t comm_id = -1);

    // ft.cpp
   public:
    int ft_thread_start();
    int ft_thread_destroy();
    static int ft_thread_loop();
    static int ft_thread_ping_pong();
    int ft_one_loop(lfi_endpoint &lfi_ep);
    int ft_cancel_comm(lfi_comm &lfi_comm);
    int ft_setup_ping_pong();
    // Fault tolerance
    std::thread ft_thread_pp;
    std::mutex ft_mutex;
    std::condition_variable ft_cv;
    bool ft_is_running = false;

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
    std::pair<lfi_msg, lfi_msg> any_recv(void *buffer_shm, void *buffer_peer, size_t size, uint32_t tag);
    int async_recv_internal(void *buffer, size_t size, recv_type type, uint32_t tag, lfi_request &request,
                            int32_t timeout_ms = -1);
    // Redirects
    lfi_msg recv(uint32_t comm_id, void *buffer, size_t size, uint32_t tag) {
        return recv_internal(comm_id, buffer, size, recv_type::RECV, tag);
    }
    lfi_msg recvv(uint32_t comm_id, struct iovec *iov, size_t count, uint32_t tag) {
        return recv_internal(comm_id, reinterpret_cast<void *>(iov), count, recv_type::RECVV, tag);
    }
    int async_recv(void *buffer, size_t size, uint32_t tag, lfi_request &request, int32_t timeout_ms = -1) {
        return async_recv_internal(buffer, size, recv_type::RECV, tag, request, timeout_ms);
    }
    int async_recvv(struct iovec *iov, size_t count, uint32_t tag, lfi_request &request, int32_t timeout_ms = -1) {
        return async_recv_internal(reinterpret_cast<void *>(iov), count, recv_type::RECVV, tag, request, timeout_ms);
    }
    lfi_msg recv_peek(uint32_t comm_id, void *buffer, size_t size, uint32_t tag);

    // send.cpp
   public:
    enum class send_type {
        SEND,
        SENDV,
    };
    lfi_msg send_internal(uint32_t comm_id, const void *ptr, size_t size, send_type type, uint32_t tag);
    int async_send_internal(const void *buffer, size_t size, send_type type, uint32_t tag, lfi_request &request,
                            int32_t timeout_ms = -1);
    // Redirects
    lfi_msg send(uint32_t comm_id, const void *buffer, size_t size, uint32_t tag) {
        return send_internal(comm_id, buffer, size, send_type::SEND, tag);
    }
    lfi_msg sendv(uint32_t comm_id, const struct iovec *iov, size_t count, uint32_t tag) {
        return send_internal(comm_id, reinterpret_cast<const void *>(iov), count, send_type::SENDV, tag);
    }
    int async_send(const void *buffer, size_t size, uint32_t tag, lfi_request &request, int32_t timeout_ms = -1) {
        return async_send_internal(buffer, size, send_type::SEND, tag, request, timeout_ms);
    }
    int async_sendv(const struct iovec *iov, size_t count, uint32_t tag, lfi_request &request,
                    int32_t timeout_ms = -1) {
        return async_send_internal(reinterpret_cast<const void *>(iov), count, send_type::SENDV, tag, request,
                                   timeout_ms);
    }
    // wait.cpp
   private:
    inline bool wait_check_timeout(int32_t timeout_ms, decltype(std::chrono::high_resolution_clock::now()) start);
    void wake_up_requests(lfi_endpoint &ep);

   public:
    int wait(lfi_request &request, int32_t timeout_ms = -1);
    int wait_num(std::vector<lfi_request *> &request, int how_many, int32_t timeout_ms = -1);

   public:
    void dump_stats();
    // Variables
   public:
    lfi_endpoint shm_ep{*this, true};
    lfi_endpoint peer_ep{*this, false};

    std::mutex m_fut_mutex;
    std::unordered_map<uint32_t, std::future<uint32_t>> m_fut_comms;
    std::mutex m_comms_mutex;
    std::unordered_map<uint32_t, std::unique_ptr<lfi_comm>> m_comms;
    std::atomic_uint32_t m_rank_counter = {0};

   public:
    static inline LFI &get_instance() {
        static LFI instance;
        return instance;
    }
};

}  // namespace LFI