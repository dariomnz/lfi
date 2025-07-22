
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

lfi_msg LFI::send_internal(uint32_t comm_id, const void *ptr, size_t size, send_type type, uint32_t tag) {
    lfi_msg msg = {};
    int ret = 0;
    debug_info("[LFI] Start");

    // Check if any_comm in send is error
    if (comm_id == ANY_COMM_SHM || comm_id == ANY_COMM_PEER) {
        msg.error = -LFI_SEND_ANY_COMM;
        return msg;
    }

    // Check if comm exists
    std::shared_ptr<lfi_comm> comm = get_comm(comm_id);
    if (!comm) {
        msg.error = -LFI_COMM_NOT_FOUND;
        return msg;
    }
    lfi_request request(comm);

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
                             int32_t timeout_ms) {
    int ret;
    uint32_t run_loop = 0;
#ifdef DEBUG
    defer([&run_loop] { debug_info("[LFI] run_loop " << run_loop << " times in async send"); });
#endif
    // Check cancelled comm
    if (request.m_comm->is_canceled) {
        return -LFI_BROKEN_COMM;
    }

    // Check if any_comm in send is error
    if (request.m_comm->rank_peer == ANY_COMM_SHM || request.m_comm->rank_peer == ANY_COMM_PEER) {
        return -LFI_SEND_ANY_COMM;
    }

    std::unique_lock req_lock(request.mutex);
    request.reset();

    decltype(std::chrono::high_resolution_clock::now()) start;
    if (timeout_ms >= 0) {
        start = std::chrono::high_resolution_clock::now();
    }
    // tag format 24 bits rank_peer 24 bits rank_self_in_peer 16 bits tag
    uint64_t aux_rank_self_in_peer = request.m_comm->rank_self_in_peer;
    uint64_t aux_tag = tag;
    uint64_t tag_send = (aux_rank_self_in_peer << MASK_RANK_BYTES) | aux_tag;

    if (env::get_instance().LFI_fault_tolerance && request.m_comm->rank_peer != ANY_COMM_SHM &&
        request.m_comm->rank_peer != ANY_COMM_PEER) {
        std::unique_lock lock(request.m_comm->m_ep.requests_mutex);
        request.m_comm->m_ep.ft_comms.emplace(request.m_comm);
        request.m_comm->ft_comm_count++;
    }

    debug_info("[LFI] Start size " << size << " rank_peer " << request.m_comm->rank_peer << " rank_self_in_peer "
                                   << request.m_comm->rank_self_in_peer << " tag " << tag << " send_context "
                                   << (void *)&request.context);

    request.is_send = true;
    if (env::get_instance().LFI_use_inject && type == send_type::SEND &&
        size <= request.m_comm->m_ep.info->tx_attr->inject_size) {
        fid_ep *p_tx_ep = request.m_comm->m_ep.use_scalable_ep ? request.m_comm->m_ep.tx_ep : request.m_comm->m_ep.ep;
        request.wait_context = false;
        request.is_inject = true;
        do {
            ret = fi_tinject(p_tx_ep, buffer, size, request.m_comm->fi_addr, tag_send);

            if (ret == -FI_EAGAIN) {
                req_lock.unlock();
                protected_progress(request.m_comm->m_ep);
                req_lock.lock();

                if (timeout_ms >= 0) {
                    int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::high_resolution_clock::now() - start)
                                             .count();
                    if (elapsed_ms >= timeout_ms) {
                        return -LFI_TIMEOUT;
                    }
                }

                if (request.m_comm->is_canceled) {
                    return -LFI_BROKEN_COMM;
                }
            }
            run_loop++;
        } while (ret == -FI_EAGAIN);

        // To not wait in this request
        debug_info("[LFI] fi_tinject of " << size << " for rank_peer " << request.m_comm->rank_peer);
    } else {
        fid_ep *p_tx_ep = request.m_comm->m_ep.use_scalable_ep ? request.m_comm->m_ep.tx_ep : request.m_comm->m_ep.ep;
        request.wait_context = true;
        request.is_inject = false;
        do {
            if (type == send_type::SEND) {
                ret = fi_tsend(p_tx_ep, buffer, size, NULL, request.m_comm->fi_addr, tag_send, &request.context);
            } else if (type == send_type::SENDV) {
                ret = fi_tsendv(p_tx_ep, reinterpret_cast<const iovec *>(buffer), NULL, size, request.m_comm->fi_addr,
                                tag_send, &request.context);
            } else {
                std::runtime_error("Error unknown send_type. This should not happend");
            }

            if (ret == -FI_EAGAIN) {
                req_lock.unlock();
                protected_progress(request.m_comm->m_ep);
                req_lock.lock();

                if (timeout_ms >= 0) {
                    int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::high_resolution_clock::now() - start)
                                             .count();
                    if (elapsed_ms >= timeout_ms) {
                        return -LFI_TIMEOUT;
                    }
                }

                if (request.m_comm->is_canceled) {
                    return -LFI_BROKEN_COMM;
                }
            }
            run_loop++;
        } while (ret == -FI_EAGAIN);

        if (env::get_instance().LFI_fault_tolerance && ret == 0) {
            std::unique_lock fi_lock(request.m_comm->ft_mutex);
            debug_info("[LFI] insert request " << std::hex << &request << std::dec << " in comm "
                                               << request.m_comm->rank_peer);
            request.m_comm->ft_requests.insert(&request);
        }

        debug_info("[LFI] Waiting on rank_peer " << request.m_comm->rank_peer);
    }

    if (ret != 0) {
        printf("error posting send buffer (%d)\n", ret);
        return -LFI_LIBFABRIC_ERROR;
    }

    request.size = size;
    request.tag = tag;
    request.source = request.m_comm->rank_peer;

    debug_info("[LFI] msg size " << request.size << " source " << request.source << " tag " << request.tag << " error "
                                 << request.error);
    debug_info("[LFI] End = " << size);
    return LFI_SUCCESS;
}
}  // namespace LFI