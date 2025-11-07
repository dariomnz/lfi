
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

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_set>

#include "lfi.h"

namespace LFI {

// Forward declaration
struct lfi_request;
struct lfi_endpoint;

constexpr static const uint32_t UNINITIALIZED_COMM = 0xFFFFFFFF;
constexpr static const uint32_t ANY_COMM_SHM = LFI_ANY_COMM_SHM;
constexpr static const uint32_t ANY_COMM_PEER = LFI_ANY_COMM_PEER;

struct lfi_comm {
    uint32_t rank_peer;
    uint32_t rank_self_in_peer;

    fi_addr_t fi_addr = FI_ADDR_UNSPEC;

    lfi_endpoint &m_ep;

    // For fault tolerance
    std::mutex ft_mutex;
    std::unordered_set<lfi_request *> ft_requests;
    using clock = std::chrono::high_resolution_clock;
    uint32_t ft_comm_count = 0;
    std::chrono::time_point<clock> ft_ping_time_point = {}, ft_pong_time_point = {};
    std::unique_ptr<lfi_request> ft_ping = nullptr, ft_pong = nullptr;
    std::chrono::time_point<clock> last_request_time = clock::now();
    enum ft_status {
        IDLE,
        SEND_PING,
        RECV_PONG,
        WAIT_PING_PONG,
        ERROR,
    };
    ft_status ft_current_status = ft_status::IDLE;

   public:
    bool ft_error = false;

    bool is_canceled = false;

    std::atomic_bool is_ready = false;
    std::atomic_bool in_fut = false;

    lfi_comm(lfi_endpoint &ep) : m_ep(ep) {}

    void update_request_time() {
        std::unique_lock lock(ft_mutex);
        last_request_time = clock::now();
    }

    auto get_request_time() {
        std::unique_lock lock(ft_mutex);
        return last_request_time;
    }
};
}  // namespace LFI