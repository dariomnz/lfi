
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

#include "impl/debug.hpp"
#include "impl/env.hpp"
#include "impl/ft_manager.hpp"
#include "impl/lfi.hpp"
#include "impl/profiler.hpp"

namespace LFI {

lfi_msg LFI::send_internal(uint32_t comm_id, const void *ptr, size_t size, send_type type, uint32_t tag) {
    LFI_PROFILE_FUNCTION();
    lfi_msg msg = {};
    int ret = 0;
    debug_info("[LFI] Start");

    // Check if any_comm in send is error
    if (comm_id == ANY_COMM_SHM || comm_id == ANY_COMM_PEER) {
        msg.error = -LFI_SEND_ANY_COMM;
        return msg;
    }

    // Check if comm exists
    auto [lock, comm] = get_comm_and_mutex(comm_id);
    if (!comm) {
        msg.error = -LFI_COMM_NOT_FOUND;
        return msg;
    }
    lfi_request request(comm->m_endpoint, comm->rank_peer);

    lock.unlock();

    switch (type) {
        case send_type::SEND:
            ret = async_send(ptr, size, tag, request);
            break;
        case send_type::SENDV:
            ret = async_sendv(reinterpret_cast<const iovec *>(ptr), size, tag, request);
            break;
        default:
            std::runtime_error("Error unknown recv_type. This should not happend");
            break;
    }

    if (ret < 0) {
        msg.error = ret;
        return msg;
    }

    wait(request);

    debug_info("[LFI] End");
    return request;
}

int LFI::async_send_internal(const void *buffer, size_t size, send_type type, uint32_t tag, lfi_request &request,
                             bool priority) {
    LFI_PROFILE_FUNCTION();
    auto comm_id = request.m_comm_id;
    auto [lock, comm] = get_comm_and_mutex(comm_id);
    std::unique_lock req_lock(request.mutex);
    // Check if comm is found
    if (!comm) {
        return -LFI_COMM_NOT_FOUND;
    }

    // Check cancelled comm
    if (comm->is_canceled) {
        return -LFI_BROKEN_COMM;
    }

    // Check if any_comm in send is error
    if (request.m_comm_id == ANY_COMM_SHM || request.m_comm_id == ANY_COMM_PEER) {
        return -LFI_SEND_ANY_COMM;
    }

    request.reset();

    // tag format 24 bits rank_peer 24 bits rank_self_in_peer 16 bits tag
    uint64_t aux_rank_self_in_peer = comm->rank_self_in_peer;
    uint64_t aux_tag = tag;
    uint64_t tag_send = (aux_rank_self_in_peer << MASK_RANK_BYTES) | aux_tag;

    debug_info("[LFI] Start size " << size << " rank_peer " << format_lfi_comm{request.m_comm_id}
                                   << " rank_self_in_peer " << comm->rank_self_in_peer << " tag " << format_lfi_tag{tag}
                                   << " send_context " << request.wait_context);

    request.size = size;
    request.tag = tag;
    request.source = request.m_comm_id;
    fid_ep *p_tx_ep = comm->m_endpoint.tx_endpoint();
    lfi_pending_op::Type op_type;

    if (env::get_instance().LFI_use_inject && type == send_type::SEND &&
        size <= comm->m_endpoint.info->tx_attr->inject_size) {
        op_type = lfi_pending_op::Type::INJECT;
        request.op_type = lfi_request::OpType::INJECT;
    } else {
        op_type = (type == send_type::SEND) ? lfi_pending_op::Type::SEND : lfi_pending_op::Type::SENDV;
        request.op_type = lfi_request::OpType::SEND;
    }

    if (!request.wait_context.load()) {
        request.wait_context.store(req_ctx_factory.create(request));
    }

    {
        std::unique_lock lock_pending(comm->m_endpoint.pending_ops_mutex);
        debug_info("[LFI] Save send to " << (priority ? "priority_ops " : "pending_ops ") << request);
        auto &queue = priority ? comm->m_endpoint.priority_ops : comm->m_endpoint.pending_ops;
        queue.push({op_type, p_tx_ep, {buffer}, size, comm->fi_addr, tag_send, 0, request.wait_context.load()});
    }

    debug_info("[LFI] msg " << request);

    req_lock.unlock();
    m_ft_manager.register_request(&request, tag, comm);

    // comm->m_endpoint.protected_progress(true);

    debug_info("[LFI] End = " << size);
    return LFI_SUCCESS;
}
}  // namespace LFI