
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
#include "impl/lfi.hpp"
#include "sstream"

namespace LFI {

lfi_msg LFI::send(uint32_t comm_id, const void *buffer, size_t size, uint32_t tag) {
    lfi_msg msg = {};
    debug_info("[LFI] Start");

    // Check if any_comm in send is error
    if (comm_id == ANY_COMM_SHM || comm_id == ANY_COMM_PEER) {
        msg.error = -LFI_ERROR;
        return msg;
    }

    // Check if comm exists
    lfi_comm *comm = get_comm(comm_id);
    if (comm == nullptr) {
        msg.error = -LFI_COMM_NOT_FOUND;
        return msg;
    }
    lfi_request request(*comm);

    msg = async_send(buffer, size, tag, request);

    if (msg.error < 0) {
        return msg;
    }

    wait(request);

    msg.error = request.error;

    debug_info("[LFI] End");
    return msg;
}

lfi_msg LFI::async_send(const void *buffer, size_t size, uint32_t tag, lfi_request &request, int32_t timeout_ms) {
    int ret;
    lfi_msg msg = {};

    // Check cancelled comm
    if (request.m_comm.is_canceled) {
        msg.error = -LFI_CANCELED_COMM;
        return msg;
    }

    // Check if any_comm in send is error
    if (request.m_comm.rank_peer == ANY_COMM_SHM || request.m_comm.rank_peer == ANY_COMM_PEER) {
        msg.error = -LFI_ERROR;
        return msg;
    }

    request.reset();

    decltype(std::chrono::high_resolution_clock::now()) start;
    if (timeout_ms >= 0) {
        start = std::chrono::high_resolution_clock::now();
    }
    // tag format 24 bits rank_peer 24 bits rank_self_in_peer 16 bits tag
    uint64_t aux_rank_peer = request.m_comm.rank_peer;
    uint64_t aux_rank_self_in_peer = request.m_comm.rank_self_in_peer;
    uint64_t aux_tag = tag;
    uint64_t tag_send = (aux_rank_peer << 40) | (aux_rank_self_in_peer << 16) | aux_tag;

    debug_info("[LFI] Start size " << size << " rank_peer " << request.m_comm.rank_peer << " rank_self_in_peer "
                                   << request.m_comm.rank_self_in_peer << " tag " << tag << " send_context "
                                   << (void *)&request.context);

    request.is_send = true;
    if (env::get_instance().LFI_use_inject && size <= request.m_comm.m_ep.info->tx_attr->inject_size) {
        fid_ep *p_tx_ep = request.m_comm.m_ep.use_scalable_ep ? request.m_comm.m_ep.tx_ep : request.m_comm.m_ep.ep;
        request.wait_context = false;
        request.is_inject = true;
        do {
            ret = fi_tinject(p_tx_ep, buffer, size, request.m_comm.fi_addr, tag_send);

            if (ret == -FI_EAGAIN) {
                std::unique_lock ep_lock(request.m_comm.m_ep.mutex_ep, std::defer_lock);
                if (ep_lock.try_lock()) {
                    progress(request);
                    ep_lock.unlock();
                }

                if (timeout_ms >= 0) {
                    int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::high_resolution_clock::now() - start)
                                             .count();
                    if (elapsed_ms >= timeout_ms) {
                        msg.error = -LFI_TIMEOUT;
                        return msg;
                    }
                }

                if (request.m_comm.is_canceled) {
                    msg.error = -LFI_CANCELED_COMM;
                    return msg;
                }
            }
        } while (ret == -FI_EAGAIN);

        // To not wait in this request
        debug_info("[LFI] fi_tinject of " << size << " for rank_peer " << request.m_comm.rank_peer);
    } else {
        fid_ep *p_tx_ep = request.m_comm.m_ep.use_scalable_ep ? request.m_comm.m_ep.tx_ep : request.m_comm.m_ep.ep;
        request.wait_context = true;
        request.is_inject = false;
        do {
            ret = fi_tsend(p_tx_ep, buffer, size, NULL, request.m_comm.fi_addr, tag_send, &request.context);

            if (ret == -FI_EAGAIN) {
                std::unique_lock ep_lock(request.m_comm.m_ep.mutex_ep, std::defer_lock);
                if (ep_lock.try_lock()) {
                    progress(request);
                    ep_lock.unlock();
                }

                if (timeout_ms >= 0) {
                    int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::high_resolution_clock::now() - start)
                                             .count();
                    if (elapsed_ms >= timeout_ms) {
                        msg.error = -LFI_TIMEOUT;
                        return msg;
                    }
                }

                if (request.m_comm.is_canceled) {
                    msg.error = -LFI_CANCELED_COMM;
                    return msg;
                }
            }
        } while (ret == -FI_EAGAIN);

        if (env::get_instance().LFI_fault_tolerance && ret == 0) {
            std::unique_lock fi_lock(request.m_comm.ft_mutex);
            debug_info("[LFI] insert request " << std::hex << &request << std::dec << " in comm "
                                               << request.m_comm.rank_peer);
            request.m_comm.ft_requests.insert(&request);
        }

        debug_info("[LFI] Waiting on rank_peer " << request.m_comm.rank_peer);
    }

    if (ret != 0) {
        printf("error posting send buffer (%d)\n", ret);
        msg.error = -LFI_ERROR;
        return msg;
    }

    msg.size = size;
    msg.tag = tag_send & 0x0000'0000'0000'FFFF;
    msg.rank_peer = (tag_send & 0xFFFF'FF00'0000'0000) >> 40;
    msg.rank_self_in_peer = (tag_send & 0x0000'00FF'FFFF'0000) >> 16;

    debug_info("[LFI] msg size " << msg.size << " rank_peer " << msg.rank_peer << " rank_self_in_peer "
                                 << msg.rank_self_in_peer << " tag " << msg.tag << " error " << msg.error);
    debug_info("[LFI] End = " << size);
    return msg;
}
}  // namespace LFI