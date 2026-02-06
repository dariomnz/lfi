
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

#include <rdma/fi_endpoint.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "lfi_async.h"
#include "vector_queue.hpp"

namespace LFI {

// Forward declaration
class LFI;
struct lfi_comm;
struct lfi_request;
struct wait_struct;

struct lfi_endpoint {
    LFI &m_lfi;

    lfi_endpoint(LFI &lfi, bool shm) : m_lfi(lfi), is_shm(shm) {}

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

    std::atomic_bool in_progress = {false};

    std::mutex ft_mutex = {};
    std::unordered_set<lfi_request *> ft_any_comm_requests = {};
    std::unordered_set<uint32_t> ft_pending_failed_comms = {};
    std::unordered_set<lfi_comm *> ft_comms = {};
    std::vector<std::unique_ptr<lfi_request>> ft_ping_pongs;

    std::mutex waiters_mutex = {};
    std::unordered_set<std::condition_variable *> waiters_list = {};

    std::mutex callbacks_mutex = {};
    std::vector<std::tuple<lfi_request_callback, int, void *>> callbacks = {};

    bool initialized() { return enable_ep; }

    size_t get_iov_limit() { return info->tx_attr->iov_limit; }

    bool operator==(const lfi_endpoint &other) const {
        if (this->use_scalable_ep != other.use_scalable_ep) return false;

        if (this->use_scalable_ep) {
            return this->rx_ep == other.rx_ep && this->tx_ep == other.tx_ep;
        } else {
            return this->ep == other.ep;
        }
        return false;
    }

    int protected_progress(bool call_callbacks);
    int progress(bool call_callbacks);

    struct PendingOp {
        enum class Type : uint8_t { SEND, SENDV, RECV, RECVV, INJECT };
        Type type;
        fid_ep *ep;
        union {
            const void *cbuf;
            void *buf;
        } buf;
        size_t len;
        fi_addr_t addr;
        uint64_t tag;
        uint64_t ignore;
        void *context;
    };

    std::mutex pending_ops_mutex;
    VectorQueue<PendingOp> priority_ops;
    VectorQueue<PendingOp> pending_ops;

    void post_pending_ops();

    fid_ep *rx_endpoint() { return use_scalable_ep ? rx_ep : ep; }
    fid_ep *tx_endpoint() { return use_scalable_ep ? tx_ep : ep; }
};

}  // namespace LFI