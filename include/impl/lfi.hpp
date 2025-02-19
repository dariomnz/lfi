
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
#include <vector>

#include "lfi.h"
#include "lfi_async.h"
#include "lfi_error.h"

#define DECLARE_LFI_ERROR(name, msg)  \
    static constexpr const char *name##_str = msg;

#define CASE_STR_ERROR(name) \
    case name:               \
        return name##_str;

namespace LFI {
// Error codes
DECLARE_LFI_ERROR(LFI_SUCCESS,           "Success");
DECLARE_LFI_ERROR(LFI_ERROR,             "Error");
DECLARE_LFI_ERROR(LFI_TIMEOUT,           "Timeout");
DECLARE_LFI_ERROR(LFI_CANCELED,          "Canceled");
DECLARE_LFI_ERROR(LFI_CANCELED_COMM,     "Canceled COMM");
DECLARE_LFI_ERROR(LFI_COMM_NOT_FOUND,    "COMM not found");
DECLARE_LFI_ERROR(LFI_PEEK_NO_MSG,       "No msg encounter");
DECLARE_LFI_ERROR(LFI_NOT_COMPLETED,     "Request not completed");
DECLARE_LFI_ERROR(LFI_NULL_REQUEST,      "Request is NULL");
DECLARE_LFI_ERROR(LFI_SEND_ANY_COMM,     "Use of ANY_COMM in send");
DECLARE_LFI_ERROR(LFI_LIBFABRIC_ERROR,   "Internal libfabric error");

static constexpr const char *lfi_strerror(int error) {
    switch (error) {
        CASE_STR_ERROR(LFI_SUCCESS);
        CASE_STR_ERROR(LFI_ERROR);
        CASE_STR_ERROR(LFI_TIMEOUT);
        CASE_STR_ERROR(LFI_CANCELED);
        CASE_STR_ERROR(LFI_CANCELED_COMM);
        CASE_STR_ERROR(LFI_COMM_NOT_FOUND);
        CASE_STR_ERROR(LFI_PEEK_NO_MSG);
        CASE_STR_ERROR(LFI_NOT_COMPLETED);
        CASE_STR_ERROR(LFI_NULL_REQUEST);
        CASE_STR_ERROR(LFI_SEND_ANY_COMM);
        CASE_STR_ERROR(LFI_LIBFABRIC_ERROR);
        default:
            return "Unknown";
    }
}

// Reserved tags
#define LFI_TAG_FT                  (0xFFFFFFFF - 1)
#define LFI_TAG_RECV_LD_PRELOAD     (0xFFFFFFFF - 2)
#define LFI_TAG_BUFFERED_LD_PRELOAD 100000

// Forward declaration
struct lfi_ep;
struct lfi_comm;

enum class wait_endpoint {
    NONE,
    SHM,
    PEER,
    ALL,
};

struct wait_struct {
    wait_endpoint wait_type = wait_endpoint::NONE;
    std::mutex wait_mutex = {};
    std::condition_variable wait_cv = {};
    int wait_count = 0;
};

struct lfi_msg {
    uint64_t size = 0;
    uint32_t source = 0;
    uint32_t tag = 0;
    int32_t error = 0;

    std::string to_string() {
        std::stringstream out;
        out << "lfi_msg " << " size " << size << " source " << source << " tag " << tag << " error " << error;
        return out.str();
    }
};

struct lfi_request {
    // context necesary for fabric interface
    struct fi_context context = {};

    std::shared_ptr<lfi_comm> m_comm;
    std::mutex mutex = {};
    std::condition_variable cv = {};
    int error = 0;
    bool wait_context = true;

    bool is_send = false;
    bool is_inject = false;

    size_t size = 0;
    uint32_t tag = 0;
    uint32_t source = -1;

    wait_struct *shared_wait_struct = nullptr;
    lfi_request() = delete;
    lfi_request(std::shared_ptr<lfi_comm> comm) : m_comm(comm) {}
    lfi_request(const lfi_request &&request) : m_comm(request.m_comm) {}

    void reset() {
        wait_context = true;
        error = 0;
        size = 0;
        tag = 0;
        source = -1;
    }

    bool is_completed() { return !wait_context; }

    std::string to_string() {
        std::stringstream out;
        out << "Request " << std::hex << this;
        if (is_send) {
            out << " is_send ";
        }
        if (is_inject) {
            out << "is_inject";
        }
        return out.str();
    }

    operator lfi_msg() const { return lfi_msg{.size = size, .source = source, .tag = tag, .error = error}; }
};

struct lfi_comm {
    uint32_t rank_peer;
    uint32_t rank_self_in_peer;

    fi_addr_t fi_addr = FI_ADDR_UNSPEC;

    lfi_ep &m_ep;

    // For fault tolerance
    std::recursive_mutex ft_mutex;
    std::unordered_set<lfi_request *> ft_requests;
    bool ft_error = false;

    bool is_canceled = false;

    std::atomic_bool is_ready = false;

    lfi_comm(lfi_ep &ep) : m_ep(ep) {}
};

struct lfi_ep {
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

    bool initialized() { return enable_ep; }

    size_t get_iov_limit() { return info->tx_attr->iov_limit; }

    bool operator==(const lfi_ep &other) const {
        if (this->use_scalable_ep != other.use_scalable_ep) return false;

        if (this->use_scalable_ep) {
            return this->rx_ep == other.rx_ep && this->tx_ep == other.tx_ep;
        } else {
            return this->ep == other.ep;
        }
        return false;
    }
};

// Constants
constexpr static const uint32_t ANY_COMM_SHM = LFI_ANY_COMM_SHM;
constexpr static const uint32_t ANY_COMM_PEER = LFI_ANY_COMM_PEER;

constexpr static const uint64_t MASK_RANK = 0xFFFF'FFFF'0000'0000;
constexpr static const uint64_t MASK_RANK_BYTES = 32;
constexpr static const uint64_t MASK_TAG = 0x0000'0000'FFFF'FFFF;
constexpr static const uint64_t MASK_TAG_BYTES = 32;

class LFI {
    // address.cpp
   public:
    int get_addr(std::shared_ptr<lfi_comm> lfi_comm, std::vector<uint8_t> &out_addr);
    int register_addr(std::shared_ptr<lfi_comm> lfi_comm, std::vector<uint8_t> &addr);
    int remove_addr(std::shared_ptr<lfi_comm> lfi_comm);

    // cancel.cpp
   public:
    int cancel(lfi_request &request);

    // comm.cpp
   public:
    uint32_t reserve_comm();
    std::shared_ptr<lfi_comm> init_comm(bool is_shm, int32_t comm_id = -1);
    std::shared_ptr<lfi_comm> get_comm(uint32_t id);
    int close_comm(uint32_t id);

   private:
    std::shared_ptr<lfi_comm> create_comm(lfi_ep &lfi_ep, int32_t comm_id = -1);
    std::shared_ptr<lfi_comm> create_any_comm(lfi_ep &lfi_ep, uint32_t comm_id);

    // connection.cpp
   public:
    int init_server(int socket, int32_t comm_id = -1);
    int init_client(int socket, int32_t comm_id = -1);

    // ft.cpp
   public:
    int ft_thread_start();
    int ft_thread_destroy();
    static int ft_thread_loop();
    // Fault tolerance
    std::thread ft_thread;
    std::mutex ft_mutex;
    std::condition_variable ft_cv;
    bool ft_is_running = false;

    // init.cpp
   public:
    LFI();
    // Secure close ep when closing app
    ~LFI();

   private:
    int set_hints(lfi_ep &lfi_ep, const std::string &prov);
    int init(lfi_ep &lfi_ep);
    int destroy(lfi_ep &lfi_ep);

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
    int async_sendv(const struct iovec *iov, size_t count, uint32_t tag, lfi_request &request, int32_t timeout_ms = -1) {
        return async_send_internal(reinterpret_cast<const void *>(iov), count, send_type::SENDV, tag, request, timeout_ms);
    }
    // wait.cpp
   private:
    inline bool wait_check_timeout(int32_t timeout_ms, decltype(std::chrono::high_resolution_clock::now()) start);

   public:
    int progress(lfi_ep &lfi_ep);
    int wait(lfi_request &request, int32_t timeout_ms = -1);
    int wait_num(std::vector<std::reference_wrapper<lfi_request>> &request, int how_many, int32_t timeout_ms = -1);

    // Variables
   public:
    lfi_ep shm_ep = {.is_shm = true};
    lfi_ep peer_ep = {.is_shm = false};

    std::mutex m_fut_mutex;
    std::unordered_map<uint32_t, std::future<uint32_t>> m_fut_comms;
    std::mutex m_comms_mutex;
    std::unordered_map<uint32_t, std::shared_ptr<lfi_comm>> m_comms;
    std::atomic_uint32_t m_rank_counter = {0};

   public:
    static inline LFI &get_instance() {
        static LFI instance;
        return instance;
    }
};

}  // namespace LFI