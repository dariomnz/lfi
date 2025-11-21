
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

#include <queue>

#include "debug.hpp"
#include "lfi_request.hpp"

namespace LFI {

struct lfi_request_context {
    struct fi_context2 context = {};

   private:
    std::mutex mutex;
    lfi_request* request;

   public:
    lfi_request_context(lfi_request& req) { assign(req); }

    ~lfi_request_context() { unassign(); }

    lfi_request* get_request() {
        std::unique_lock lock(mutex);
        return request;
    }

    void assign(lfi_request& req) {
        std::unique_lock lock(mutex);
        request = &req;
    }

    void unassign() {
        std::unique_lock lock(mutex);
        request = nullptr;
    }
};

struct lfi_request_context_factory {
    lfi_request_context* create(lfi_request& req) {
        std::unique_lock lock(m_mutex);
        if (m_queue.empty()) {
            m_stat_created++;
            return new (std::nothrow) lfi_request_context(req);
        }
        lfi_request_context* obj = m_queue.back();
        m_queue.resize(m_queue.size() - 1);
        obj->assign(req);
        return obj;
    }

    void destroy(lfi_request_context* req_ctx) {
        std::unique_lock lock(m_mutex);
        req_ctx->unassign();
        m_queue.emplace_back(req_ctx);
    }

    ~lfi_request_context_factory() {
        std::unique_lock lock(m_mutex);
        for (auto&& ptr : m_queue) {
            delete ptr;
        }
        m_queue.clear();
    }

    void dump() {
        std::unique_lock lock(m_mutex);
        std::cerr << "lfi_request_context_factory created: " << m_stat_created << " in queue: " << m_queue.size()
                  << " in usage: " << (m_stat_created - m_queue.size()) << std::endl;
    }

   private:
    std::mutex m_mutex;
    std::vector<lfi_request_context*> m_queue;
    int32_t m_stat_created;
};
}  // namespace LFI