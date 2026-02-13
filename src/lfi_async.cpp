
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

#include "lfi_async.h"

#include "impl/debug.hpp"
#include "impl/lfi.hpp"
#include "impl/profiler.hpp"
#include "lfi_error.h"
#include "lfi_request.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void lfi_request_set_callback(lfi_request *req, lfi_request_callback func_ptr, void *context) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return;
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    std::unique_lock request_lock(request->mutex);
    if (func_ptr) {
        request->callback = func_ptr;
        request->callback_ctx = context;
        debug_info("Setting callback to func_ptr wth context");
    } else {
        request->callback = nullptr;
        request->callback_ctx = nullptr;
    }
    debug_info("(" << req << ")>> End");
}

lfi_request *lfi_request_create(int id) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << id << ")>> Begin");
    LFI::LFI &lfi = LFI::LFI::get_instance();
    auto [lock, comm] = lfi.get_comm_and_mutex(id);
    if (!comm) {
        debug_info("(" << id << ")=" << nullptr << " >> End");
        return nullptr;
    }
    const auto ret =
        reinterpret_cast<lfi_request *>(new (std::nothrow) LFI::lfi_request(comm->m_endpoint, comm->rank_peer));
    debug_info("(" << id << ")=" << ret << " >> End");
    return ret;
}

void lfi_request_free(lfi_request *req) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return;
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    {
        std::unique_lock request_lock(request->mutex);
        if (!request->is_completed()) {
            request_lock.unlock();
            debug_info(*request);
            request->cancel();
        }
    }
    debug_info("(" << req << ")>> End");
    delete request;
}

bool lfi_request_completed(lfi_request *req) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return false;
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(*request);
    std::unique_lock request_lock(request->mutex);
    const bool ret = request->is_completed();
    debug_info("(" << req << ")=" << ret << " >> End");
    return ret;
}

ssize_t lfi_request_size(lfi_request *req) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return -LFI_NULL_REQUEST;
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(*request);
    std::unique_lock request_lock(request->mutex);
    ssize_t ret;
    if (request->is_completed()) {
        if (request->error) {
            ret = request->error;
        } else {
            ret = request->size;
        }
    } else {
        ret = -LFI_NOT_COMPLETED;
    }
    debug_info("(" << req << ")=" << ret << " >> End");
    return ret;
}

ssize_t lfi_request_source(lfi_request *req) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return -LFI_NULL_REQUEST;
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(*request);
    std::unique_lock request_lock(request->mutex);
    const auto ret = request->is_completed() ? request->source : -LFI_NOT_COMPLETED;
    debug_info("(" << req << ")=" << ret << " >> End");
    return ret;
}

ssize_t lfi_request_error(lfi_request *req) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return -LFI_NULL_REQUEST;
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(*request);
    std::unique_lock request_lock(request->mutex);
    const auto ret = request->is_completed() ? request->error : -LFI_NOT_COMPLETED;
    debug_info("(" << req << ")=" << ret << " >> End");
    return ret;
}

ssize_t lfi_send_async(lfi_request *req, const void *data, size_t size) {
    LFI_PROFILE_FUNCTION();
    return lfi_tsend_async(req, data, size, 0);
}

ssize_t lfi_recv_async(lfi_request *req, void *data, size_t size) {
    LFI_PROFILE_FUNCTION();
    return lfi_trecv_async(req, data, size, 0);
}

ssize_t lfi_tsend_async(lfi_request *req, const void *data, size_t size, int tag) {
    LFI_PROFILE_FUNCTION();
    ssize_t ret = 0;
    debug_info("(" << req << ", " << data << ", " << size << ", " << tag << ")>> Begin");
    if (req == nullptr) return -LFI_NULL_REQUEST;
    LFI::LFI &lfi = LFI::LFI::get_instance();
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(*request);
    ret = lfi.async_send(data, size, tag, *request);
    debug_info("(" << request << ", " << data << ", " << size << ", " << tag << ")=" << ret << ">> End");
    return ret;
}

ssize_t lfi_trecv_async(lfi_request *req, void *data, size_t size, int tag) {
    LFI_PROFILE_FUNCTION();
    ssize_t ret = 0;
    debug_info("(" << req << ", " << data << ", " << size << ", " << tag << ")>> Begin");
    if (req == nullptr) return -LFI_NULL_REQUEST;
    LFI::LFI &lfi = LFI::LFI::get_instance();
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(*request);
    ret = lfi.async_recv(data, size, tag, *request);
    debug_info("(" << request << ", " << data << ", " << size << ", " << tag << ")=" << ret << ">> End");
    return ret;
}

ssize_t lfi_put_async(lfi_request *req, const void *data, size_t size, uint64_t remote_addr, uint64_t remote_key) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << req << ", " << data << ", " << size << ", " << remote_addr << ", " << remote_key << ")>> Begin");
    if (req == nullptr) return -LFI_NULL_REQUEST;
    LFI::LFI &lfi = LFI::LFI::get_instance();
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(*request);
    const auto ret = lfi.async_put(data, size, remote_addr, remote_key, *request);
    debug_info("(" << request << ", " << data << ", " << size << ", " << remote_addr << ", " << remote_key
                   << ")=" << ret << " >> End");
    return ret;
}

ssize_t lfi_get_async(lfi_request *req, void *data, size_t size, uint64_t remote_addr, uint64_t remote_key) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << req << ", " << data << ", " << size << ", " << remote_addr << ", " << remote_key << ")>> Begin");
    if (req == nullptr) return -LFI_NULL_REQUEST;
    LFI::LFI &lfi = LFI::LFI::get_instance();
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(*request);
    const auto ret = lfi.async_get(data, size, remote_addr, remote_key, *request);
    debug_info("(" << request << ", " << data << ", " << size << ", " << remote_addr << ", " << remote_key
                   << ")=" << ret << " >> End");
    return ret;
}

ssize_t lfi_wait(lfi_request *req) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return -LFI_NULL_REQUEST;
    LFI::LFI &lfi = LFI::LFI::get_instance();
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(*request);
    const auto ret = lfi.wait(*request);
    debug_info("(" << req << ")=" << ret << " >> End");
    return ret;
}

inline ssize_t lfi_wait_wrapper(lfi_request *reqs[], size_t size, size_t how_many) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << reqs << ", " << size << ", " << how_many << ")>> Begin");
    if (reqs == nullptr) return -LFI_NULL_REQUEST;
    LFI::LFI &lfi = LFI::LFI::get_instance();
    LFI::lfi_request **requests = reinterpret_cast<LFI::lfi_request **>(reqs);
    const ssize_t ret = lfi.wait_num(requests, size, how_many);
    debug_info("(" << reqs << ", " << size << ", " << how_many << ")=" << ret << ">> End");
    return ret;
}

ssize_t lfi_wait_any(lfi_request *reqs[], size_t size) {
    LFI_PROFILE_FUNCTION();
    return lfi_wait_wrapper(reqs, size, 1);
}

ssize_t lfi_wait_all(lfi_request *reqs[], size_t size) {
    LFI_PROFILE_FUNCTION();
    return lfi_wait_wrapper(reqs, size, size);
}

ssize_t lfi_test(lfi_request *req) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return -LFI_NULL_REQUEST;
    LFI::LFI &lfi = LFI::LFI::get_instance();
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(*request);
    const auto ret = lfi.test(*request);
    debug_info("(" << req << ")=" << ret << " >> End");
    return ret;
}

inline ssize_t lfi_test_wrapper(lfi_request *reqs[], size_t size, size_t how_many) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << reqs << ", " << size << ", " << how_many << ")>> Begin");
    if (reqs == nullptr) return -LFI_NULL_REQUEST;
    LFI::LFI &lfi = LFI::LFI::get_instance();
    LFI::lfi_request **requests = reinterpret_cast<LFI::lfi_request **>(reqs);
    const ssize_t ret = lfi.test_num(requests, size, how_many);
    debug_info("(" << reqs << ", " << size << ", " << how_many << ")=" << ret << ">> End");
    return ret;
}

ssize_t lfi_test_any(lfi_request *reqs[], size_t size) {
    LFI_PROFILE_FUNCTION();
    return lfi_test_wrapper(reqs, size, 1);
}

ssize_t lfi_test_all(lfi_request *reqs[], size_t size) {
    LFI_PROFILE_FUNCTION();
    return lfi_test_wrapper(reqs, size, size);
}

ssize_t lfi_cancel(lfi_request *req) {
    LFI_PROFILE_FUNCTION();
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return -LFI_NULL_REQUEST;
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(*request);
    request->cancel();
    debug_info("(" << req << ")=" << 0 << " >> End");
    return 0;
}

#ifdef __cplusplus
}
#endif