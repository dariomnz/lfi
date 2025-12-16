
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
                             int32_t timeout_ms) {
    LFI_PROFILE_FUNCTION();
    int ret;
#ifdef DEBUG
    uint32_t run_loop = 0;
    defer([&run_loop] { debug_info("[LFI] run_loop " << run_loop << " times in async send"); });
#endif
    std::unique_lock req_lock(request.mutex);
    auto [lock, comm] = get_comm_and_mutex(request.m_comm_id);

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

    decltype(std::chrono::high_resolution_clock::now()) start;
    if (timeout_ms >= 0) {
        start = std::chrono::high_resolution_clock::now();
    }
    // tag format 24 bits rank_peer 24 bits rank_self_in_peer 16 bits tag
    uint64_t aux_rank_self_in_peer = comm->rank_self_in_peer;
    uint64_t aux_tag = tag;
    uint64_t tag_send = (aux_rank_self_in_peer << MASK_RANK_BYTES) | aux_tag;

    if (env::get_instance().LFI_fault_tolerance && request.m_comm_id != ANY_COMM_SHM &&
        request.m_comm_id != ANY_COMM_PEER) {
        req_lock.unlock();
        {
            std::scoped_lock lock(comm->m_endpoint.ft_mutex, comm->ft_mutex);
            comm->m_endpoint.ft_comms.emplace(comm);
            comm->ft_comm_count++;
        }
        req_lock.lock();
    }

    debug_info("[LFI] Start size " << size << " rank_peer " << request.m_comm_id << " rank_self_in_peer "
                                   << comm->rank_self_in_peer << " tag " << lfi_tag_to_string(tag) << " send_context "
                                   << request.wait_context);

    request.is_send = true;
    request.size = size;
    request.tag = tag;
    request.source = request.m_comm_id;

    if (env::get_instance().LFI_use_inject && type == send_type::SEND &&
        size <= comm->m_endpoint.info->tx_attr->inject_size) {
        fid_ep *p_tx_ep = comm->m_endpoint.tx_endpoint();
        auto aux_wait_context = request.wait_context.load();
        if (aux_wait_context) {
            aux_wait_context->unassign();
        }
        request.wait_context.store(nullptr);
        request.is_inject = true;
        do {
            ret = fi_tinject(p_tx_ep, buffer, size, comm->fi_addr, tag_send);

            if (ret == -FI_EAGAIN) {
                req_lock.unlock();
                comm->m_endpoint.protected_progress(false);
                req_lock.lock();

                if (timeout_ms >= 0) {
                    int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::high_resolution_clock::now() - start)
                                             .count();
                    if (elapsed_ms >= timeout_ms) {
                        return -LFI_TIMEOUT;
                    }
                }

                if (comm->is_canceled) {
                    return -LFI_BROKEN_COMM;
                }
            }
#ifdef DEBUG
            run_loop++;
#endif
        } while (ret == -FI_EAGAIN);

        // To not wait in this request
        debug_info("[LFI] fi_tinject of " << size << " for rank_peer " << request.m_comm_id);
    } else {
        fid_ep *p_tx_ep = comm->m_endpoint.tx_endpoint();
        request.wait_context.store(req_ctx_factory.create(request));
        request.is_inject = false;
        do {
            if (type == send_type::SEND) {
                ret = fi_tsend(p_tx_ep, buffer, size, NULL, comm->fi_addr, tag_send, request.wait_context.load());
            } else if (type == send_type::SENDV) {
                ret = fi_tsendv(p_tx_ep, reinterpret_cast<const iovec *>(buffer), NULL, size, comm->fi_addr, tag_send,
                                request.wait_context.load());
            } else {
                std::runtime_error("Error unknown send_type. This should not happend");
            }

            if (ret == -FI_EAGAIN) {
                req_lock.unlock();
                comm->m_endpoint.protected_progress(false);
                req_lock.lock();

                if (timeout_ms >= 0) {
                    int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::high_resolution_clock::now() - start)
                                             .count();
                    if (elapsed_ms >= timeout_ms) {
                        return -LFI_TIMEOUT;
                    }
                }

                if (comm->is_canceled) {
                    return -LFI_BROKEN_COMM;
                }
            }
#ifdef DEBUG
            run_loop++;
#endif
        } while (ret == -FI_EAGAIN);

        debug_info("[LFI] msg " << request);
        if (env::get_instance().LFI_fault_tolerance && ret == 0) {
            debug_info("[LFI] insert request " << std::hex << &request << std::dec << " in comm " << request.m_comm_id);
            req_lock.unlock();
            {
                std::unique_lock ft_lock(comm->ft_mutex);
                comm->ft_requests.insert(&request);
            }
            req_lock.lock();
        }

        debug_info("[LFI] Waiting on rank_peer " << request.m_comm_id);
    }

    if (ret != 0) {
        printf("error posting send buffer %p (%d) %s\n", buffer, ret, fi_strerror(ret));
        return -LFI_LIBFABRIC_ERROR;
    }

    debug_info("[LFI] End = " << size);
    return LFI_SUCCESS;
}
}  // namespace LFI