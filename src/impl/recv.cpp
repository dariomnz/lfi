
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
#include "impl/ft_manager.hpp"
#include "impl/lfi.hpp"
#include "impl/profiler.hpp"
#include "lfi_request.hpp"

namespace LFI {

lfi_msg LFI::recv_internal(uint32_t comm_id, void *ptr, size_t size, recv_type type, uint32_t tag) {
    LFI_PROFILE_FUNCTION();
    lfi_msg msg = {};
    int ret = 0;
    debug_info("[LFI] Start");

    // Check if comm exists
    auto [lock, comm] = get_comm_and_mutex(comm_id);
    if (!comm) {
        msg.error = -LFI_COMM_NOT_FOUND;
        return msg;
    }
    lfi_request request(comm->m_endpoint, comm->rank_peer);
    lock.unlock();

    switch (type) {
        case recv_type::RECV:
            ret = async_recv(ptr, size, tag, request);
            break;
        case recv_type::RECVV:
            ret = async_recvv(reinterpret_cast<iovec *>(ptr), size, tag, request);
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

int LFI::async_recv_internal(void *buffer, size_t size, recv_type type, uint32_t tag, lfi_request &request,
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

    request.reset();
    uint64_t mask = 0;
    uint64_t aux_rank_peer = request.m_comm_id;
    uint64_t aux_tag = tag;
    uint64_t tag_recv = (aux_rank_peer << MASK_RANK_BYTES) | aux_tag;

    if (request.m_comm_id == ANY_COMM_SHM || request.m_comm_id == ANY_COMM_PEER) {
        mask = MASK_RANK;
    }

    debug_info("[LFI] Start size " << size << " rank_peer " << format_lfi_comm{request.m_comm_id}
                                   << " rank_self_in_peer " << comm->rank_self_in_peer << " tag " << format_lfi_tag{tag}
                                   << " recv_context " << request.wait_context);

    fid_ep *p_rx_ep = comm->m_endpoint.rx_endpoint();
    request.wait_context.store(req_ctx_factory.create(request));
    request.op_type = lfi_request::OpType::RECV;

    {
        std::unique_lock lock_pending(comm->m_endpoint.pending_ops_mutex);
        debug_info("[LFI] Save recv to " << (priority ? "priority_ops " : "pending_ops ") << request);
        auto &queue = priority ? comm->m_endpoint.priority_ops : comm->m_endpoint.pending_ops;
        queue.push({(type == recv_type::RECV) ? lfi_pending_op::Type::RECV : lfi_pending_op::Type::RECVV,
                    p_rx_ep,
                    {buffer},
                    size,
                    comm->fi_addr,
                    tag_recv,
                    mask,
                    request.wait_context.load()});
    }

    request.size = size;
    request.tag = tag;
    request.source = request.m_comm_id;

    debug_info("[LFI] msg size " << request);
    req_lock.unlock();
    m_ft_manager.register_request(&request, tag, comm);

    // comm->m_endpoint.protected_progress(true);

    debug_info("[LFI] End = " << size);
    return LFI_SUCCESS;
}

}  // namespace LFI