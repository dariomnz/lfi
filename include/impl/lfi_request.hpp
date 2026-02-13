
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

#include <condition_variable>
#include <mutex>
#include <sstream>

#include "lfi_async.h"
#include "lfi_comm.hpp"

namespace LFI {

struct format_lfi_tag {
    uint64_t tag;
    friend std::ostream &operator<<(std::ostream &os, const format_lfi_tag &tag);
};

struct format_fi_tag {
    uint64_t tag;
    friend std::ostream &operator<<(std::ostream &os, const format_fi_tag &tag);
};

struct wait_struct {
    std::mutex wait_mutex = {};
    std::condition_variable wait_cv = {};
    std::atomic<int> wait_count = 0;
};

struct lfi_msg {
    uint64_t size = 0;
    uint32_t source = 0;
    uint32_t tag = 0;
    int32_t error = 0;

    std::string to_string() {
        std::stringstream out;
        out << "lfi_msg "
            << " size " << size << " source " << source << " tag " << tag << " error " << error;
        return out.str();
    }
};

// Forward declaration
struct lfi_request_context;
struct lfi_request {
    lfi_endpoint &m_endpoint;
    const uint32_t m_comm_id;
    std::mutex mutex = {};
    std::condition_variable cv = {};
    int error = 0;
    // bool wait_context = true;
    std::atomic<lfi_request_context *> wait_context = nullptr;

    enum class OpType : uint8_t { NONE, SEND, INJECT, PUT, GET, RECV };
    OpType op_type = OpType::NONE;

    size_t size = 0;
    uint32_t tag = 0;
    uint32_t source = UNINITIALIZED_COMM;

    wait_struct *shared_wait_struct = nullptr;
    lfi_request_callback callback = nullptr;
    void *callback_ctx = nullptr;
    lfi_request(lfi_endpoint &endpoint, uint32_t comm_id) : m_endpoint(endpoint), m_comm_id(comm_id) {}

    // Delete default constructor
    lfi_request() = delete;
    // Delete copy constructor
    lfi_request(const lfi_request &) = delete;
    // Delete copy assignment operator
    lfi_request &operator=(const lfi_request &) = delete;
    // Delete move constructor
    lfi_request(lfi_request &&) = delete;
    // Delete move assignment operator
    lfi_request &operator=(lfi_request &&) = delete;

    void reset();

    bool is_iniciated() { return source != UNINITIALIZED_COMM; }

    bool is_completed() { return !wait_context.load(); }

    // Warning: the callback can free the request before the complete return
    void complete(int error);

    void cancel();

    // std::string to_string();
    friend std::ostream &operator<<(std::ostream &os, lfi_request &req);

    operator lfi_msg() const { return lfi_msg{.size = size, .source = source, .tag = tag, .error = error}; }
};
}  // namespace LFI