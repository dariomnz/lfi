
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
#include "impl/socket.hpp"
#include "lfi.h"

#ifdef __cplusplus
extern "C" {
#endif

lfi_request *lfi_request_create(int id) {
    debug_info("(" << id << ")>> Begin");
    LFI::LFI &lfi = LFI::LFI::get_instance();
    std::shared_ptr<LFI::lfi_comm> comm = lfi.get_comm(id);
    if (!comm) {
        debug_info("(" << id << ")=" << nullptr << " >> End");
        return nullptr;
    }
    const auto ret = reinterpret_cast<lfi_request *>(new (std::nothrow) LFI::lfi_request(comm));
    debug_info("(" << id << ")=" << ret << " >> End");
    return ret;
}

void lfi_request_free(lfi_request *req) {
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return;
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(request->to_string());
    delete request;
    debug_info("(" << req << ")>> End");
}

bool lfi_request_completed(lfi_request *req) {
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return false;
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(request->to_string());
    std::unique_lock request_lock(request->mutex);
    const bool ret = request->is_completed();
    debug_info("(" << req << ")=" << ret << " >> End");
    return ret;
}

ssize_t lfi_request_size(lfi_request *req) {
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return -1;
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(request->to_string());
    std::unique_lock request_lock(request->mutex);
    const auto ret = request->is_completed() ? request->entry.len : -1;
    debug_info("(" << req << ")=" << ret << " >> End");
    return ret;
}

ssize_t lfi_request_source(lfi_request *req) {
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return -1;
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(request->to_string());
    std::unique_lock request_lock(request->mutex);
    const auto ret = request->is_completed() ? ((request->entry.tag & 0x0000'00FF'FFFF'0000) >> 16) : -1;
    debug_info("(" << req << ")=" << ret << " >> End");
    return ret;
}

ssize_t lfi_request_error(lfi_request *req) {
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return -1;
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(request->to_string());
    std::unique_lock request_lock(request->mutex);
    const auto ret = request->is_completed() ? request->error : -1;
    debug_info("(" << req << ")=" << ret << " >> End");
    return ret;
}

ssize_t lfi_send_async(lfi_request *req, const void *data, size_t size) { return lfi_tsend_async(req, data, size, 0); }

ssize_t lfi_recv_async(lfi_request *req, void *data, size_t size) { return lfi_trecv_async(req, data, size, 0); }

ssize_t lfi_tsend_async(lfi_request *req, const void *data, size_t size, int tag) {
    ssize_t ret = 0;
    debug_info("(" << req << ", " << data << ", " << size << ", " << tag << ")>> Begin");
    if (req == nullptr) return -1;
    LFI::LFI &lfi = LFI::LFI::get_instance();
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(request->to_string());
    LFI::lfi_msg msg = lfi.async_send(data, size, tag, *request);
    if (msg.error < 0) {
        ret = msg.error;
    } else {
        ret = msg.size;
    }
    debug_info("(" << request << ", " << data << ", " << size << ", " << tag << ")=" << ret << ">> End");
    return ret;
}

ssize_t lfi_trecv_async(lfi_request *req, void *data, size_t size, int tag) {
    ssize_t ret = 0;
    debug_info("(" << req << ", " << data << ", " << size << ", " << tag << ")>> Begin");
    if (req == nullptr) return -1;
    LFI::LFI &lfi = LFI::LFI::get_instance();
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(request->to_string());
    LFI::lfi_msg msg = lfi.async_recv(data, size, tag, *request);
    if (msg.error < 0) {
        ret = msg.error;
    } else {
        ret = msg.size;
    }
    debug_info("(" << request << ", " << data << ", " << size << ", " << tag << ")=" << ret << ">> End");
    return ret;
}

ssize_t lfi_wait(lfi_request *req) {
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return -1;
    LFI::LFI &lfi = LFI::LFI::get_instance();
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(request->to_string());
    const auto ret = lfi.wait(*request);
    debug_info("(" << req << ")=" << ret << " >> End");
    return ret;
}

ssize_t lfi_wait_many(lfi_request *reqs[], size_t size, size_t how_many) {
    debug_info("(" << reqs << ", " << size << ", " << how_many << ")>> Begin");
    if (reqs == nullptr) return -1;
    LFI::LFI &lfi = LFI::LFI::get_instance();
    LFI::lfi_request **requests = reinterpret_cast<LFI::lfi_request **>(reqs);
    std::vector<std::reference_wrapper<LFI::lfi_request>> v_requests;
    v_requests.reserve(size);

    for (size_t i = 0; i < size; i++) {
        v_requests.emplace_back(*requests[i]);
        debug_info(requests[i]->to_string());
    }
    const ssize_t ret = lfi.wait_num(v_requests, how_many);
    debug_info("(" << reqs << ", " << size << ", " << how_many << ")=" << ret << ">> End");
    return ret;
}

ssize_t lfi_cancel(lfi_request *req) {
    debug_info("(" << req << ")>> Begin");
    if (req == nullptr) return -1;
    LFI::LFI &lfi = LFI::LFI::get_instance();
    LFI::lfi_request *request = reinterpret_cast<LFI::lfi_request *>(req);
    debug_info(request->to_string());
    const auto ret = lfi.cancel(*request);
    debug_info("(" << req << ")=" << ret << " >> End");
    return ret;
}

#ifdef __cplusplus
}
#endif