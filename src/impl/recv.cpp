
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

#include <mutex>

#include "impl/debug.hpp"
#include "impl/env.hpp"
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
    lfi_comm *comm = get_comm(comm_id);
    if (!comm) {
        msg.error = -LFI_COMM_NOT_FOUND;
        return msg;
    }
    lfi_request request(*comm);

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

int LFI::any_recv(lfi_request &req_shm, void *buffer_shm, lfi_request &req_peer, void *buffer_peer, size_t size,
                  uint32_t tag, lfi_msg &msg) {
    LFI_PROFILE_FUNCTION();
    int ret;
    debug_info("[LFI] Start");

    std::unique_lock req_shm_lock(req_shm.mutex);
    if (!req_shm.is_iniciated()) {
        req_shm_lock.unlock();
        ret = async_recv(buffer_shm, size, tag, req_shm);
        if (ret < 0) {
            msg.error = ret;
            return ret;
        }
    } else {
        req_shm_lock.unlock();
    }
    std::unique_lock req_peer_lock(req_peer.mutex);
    if (!req_peer.is_iniciated()) {
        req_peer_lock.unlock();
        ret = async_recv(buffer_peer, size, tag, req_peer);
        if (ret < 0) {
            msg.error = ret;
            return ret;
        }
    } else {
        req_peer_lock.unlock();
    }

    lfi_request *requests[2] = {&req_shm, &req_peer};
    int completed = wait_num(requests, 2, 1);

    if (completed == 0) {
        msg = req_shm;
        std::unique_lock lock(req_shm.mutex);
        req_shm.reset();
    } else if (completed == 1) {
        msg = req_peer;
        std::unique_lock lock(req_peer.mutex);
        req_peer.reset();
    } else {
        msg.error = completed;
    }
    return completed;

    debug_info("[LFI] End");
}

int LFI::async_recv_internal(void *buffer, size_t size, recv_type type, uint32_t tag, lfi_request &request,
                             int32_t timeout_ms) {
    LFI_PROFILE_FUNCTION();
    int ret;
#ifdef DEBUG
    uint32_t run_loop = 0;
    defer([&run_loop] { debug_info("[LFI] run_loop " << run_loop << " times in async recv"); });
#endif

    // Check cancelled comm
    if (request.m_comm.is_canceled) {
        return -LFI_BROKEN_COMM;
    }

    std::unique_lock req_lock(request.mutex);
    request.reset();
    uint64_t mask = 0;
    decltype(std::chrono::high_resolution_clock::now()) start;
    if (timeout_ms >= 0) {
        start = std::chrono::high_resolution_clock::now();
    }

    uint64_t aux_rank_peer = request.m_comm.rank_peer;
    uint64_t aux_tag = tag;
    uint64_t tag_recv = (aux_rank_peer << MASK_RANK_BYTES) | aux_tag;

    if (request.m_comm.rank_peer == ANY_COMM_SHM || request.m_comm.rank_peer == ANY_COMM_PEER) {
        mask = MASK_RANK;
        if (env::get_instance().LFI_fault_tolerance && tag != LFI_TAG_FT_PING && tag != LFI_TAG_FT_PONG) {
            req_lock.unlock();
            {
                std::unique_lock lock(request.m_comm.m_ep.ft_any_comm_requests_mutex);
                debug_info("Save request in any_comm_requests " << request);
                request.m_comm.m_ep.ft_any_comm_requests.emplace(&request);
            }
            req_lock.lock();
        }
    }

    if (env::get_instance().LFI_fault_tolerance && request.m_comm.rank_peer != ANY_COMM_SHM &&
        request.m_comm.rank_peer != ANY_COMM_PEER) {
        req_lock.unlock();
        {
            std::scoped_lock lock(request.m_comm.m_ep.ft_comms_mutex, request.m_comm.ft_mutex);
            request.m_comm.m_ep.ft_comms.emplace(&request.m_comm);
            request.m_comm.ft_comm_count++;
        }
        req_lock.lock();
    }

    debug_info("[LFI] Start size " << size << " rank_peer " << request.m_comm.rank_peer << " rank_self_in_peer "
                                   << request.m_comm.rank_self_in_peer << " tag " << lfi_tag_to_string(tag)
                                   << " recv_context " << request.wait_context);

    fid_ep *p_rx_ep = request.m_comm.m_ep.use_scalable_ep ? request.m_comm.m_ep.rx_ep : request.m_comm.m_ep.ep;
    request.wait_context = req_ctx_factory.create(request);
    request.is_send = false;
    do {
        if (type == recv_type::RECV) {
            ret = fi_trecv(p_rx_ep, buffer, size, NULL, request.m_comm.fi_addr, tag_recv, mask, request.wait_context);
        } else if (type == recv_type::RECVV) {
            ret = fi_trecvv(p_rx_ep, reinterpret_cast<const iovec *>(buffer), NULL, size, request.m_comm.fi_addr,
                            tag_recv, mask, request.wait_context);
        } else {
            std::runtime_error("Error unknown recv_type. This should not happend");
        }

        if (ret == -FI_EAGAIN) {
            req_lock.unlock();
            request.m_comm.m_ep.protected_progress();
            req_lock.lock();

            if (timeout_ms >= 0) {
                int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::high_resolution_clock::now() - start)
                                         .count();
                if (elapsed_ms >= timeout_ms) {
                    return -LFI_TIMEOUT;
                }
            }

            if (request.m_comm.is_canceled) {
                return -LFI_BROKEN_COMM;
            }
        }
#ifdef DEBUG
        run_loop++;
#endif
    } while (ret == -FI_EAGAIN);

    if (ret != 0) {
        printf("error posting recv buffer (%d) %s\n", ret, fi_strerror(ret));
        return -LFI_LIBFABRIC_ERROR;
    }

    debug_info("[LFI] Waiting on rank_peer " << request.m_comm.rank_peer);

    request.size = size;
    request.tag = tag;
    request.source = request.m_comm.rank_peer;

    if (env::get_instance().LFI_fault_tolerance) {
        req_lock.unlock();
        {
            std::unique_lock ft_lock(request.m_comm.ft_mutex);
            debug_info("[LFI] insert request " << std::hex << &request << std::dec << " in comm "
                                               << request.m_comm.rank_peer);
            request.m_comm.ft_requests.insert(&request);
        }
        req_lock.lock();
    }

    debug_info("[LFI] msg size " << request);
    debug_info("[LFI] End = " << size);
    return LFI_SUCCESS;
}

lfi_msg LFI::recv_peek(uint32_t comm_id, void *buffer, size_t size, uint32_t tag) {
    LFI_PROFILE_FUNCTION();
    int ret;
    lfi_msg msg = {};

    // Check if comm exists
    lfi_comm *comm = get_comm(comm_id);
    if (!comm) {
        msg.error = -LFI_COMM_NOT_FOUND;
        return msg;
    }
    lfi_request request(*comm);

    // Check cancelled comm
    if (request.m_comm.is_canceled) {
        msg.error = -LFI_BROKEN_COMM;
        return msg;
    }

    uint64_t mask = 0;

    uint64_t aux_rank_peer = request.m_comm.rank_peer;
    uint64_t aux_tag = tag;
    uint64_t tag_recv = (aux_rank_peer << MASK_RANK_BYTES) | aux_tag;

    if (request.m_comm.rank_peer == ANY_COMM_SHM || request.m_comm.rank_peer == ANY_COMM_PEER) {
        mask = MASK_RANK;
    }

    debug_info("[LFI] Start size " << size << " rank_peer " << request.m_comm.rank_peer << " rank_self_in_peer "
                                   << request.m_comm.rank_self_in_peer << " tag " << lfi_tag_to_string(tag)
                                   << " recv_context " << request.wait_context);

    fid_ep *p_rx_ep = request.m_comm.m_ep.use_scalable_ep ? request.m_comm.m_ep.rx_ep : request.m_comm.m_ep.ep;
    request.reset();
    request.is_send = false;
    request.wait_context = req_ctx_factory.create(request);
    iovec iov = {
        .iov_base = buffer,
        .iov_len = size,
    };
    fi_msg_tagged msg_to_peek = {
        .msg_iov = &iov,
        .desc = nullptr,
        .iov_count = 1,
        .addr = request.m_comm.fi_addr,
        .tag = tag_recv,
        .ignore = mask,
        .context = request.wait_context,
        .data = 0,
    };
    // First we PEEK with CLAIM to only generate one match
    do {
        ret = fi_trecvmsg(p_rx_ep, &msg_to_peek, FI_PEEK | FI_CLAIM);

        if (ret == -FI_EAGAIN) {
            request.m_comm.m_ep.protected_progress();

            if (request.m_comm.is_canceled) {
                msg.error = -LFI_BROKEN_COMM;
                return msg;
            }
        }
    } while (ret == -FI_EAGAIN);

    if (ret != 0) {
        printf("error PEEK recv buffer (%d) %s\n", ret, fi_strerror(ret));
        msg.error = -LFI_LIBFABRIC_ERROR;
        return msg;
    }

    debug_info("[LFI] Waiting for " << request);

    ret = wait(request);
    if (ret != 0) {
        printf("error waiting recv peek (%d) %s\n", ret, fi_strerror(ret));
        msg.error = ret;
        return msg;
    }
    // If the PEEK request is successfully we need to claim the content
    if (request.error == 0) {
        debug_info("[LFI] successfully PEEK, now CLAIM data");
        request.reset();
        do {
            ret = fi_trecvmsg(p_rx_ep, &msg_to_peek, FI_CLAIM);

            if (ret == -FI_EAGAIN) {
                request.m_comm.m_ep.protected_progress();

                if (request.m_comm.is_canceled) {
                    msg.error = -LFI_BROKEN_COMM;
                    return msg;
                }
            }
        } while (ret == -FI_EAGAIN);

        if (ret != 0) {
            printf("error CLAIM recv buffer (%d) %s\n", ret, fi_strerror(ret));
            msg.error = -LFI_LIBFABRIC_ERROR;
            return msg;
        }

        debug_info("[LFI] Waiting for " << request);
        ret = wait(request);
        if (ret != 0) {
            printf("error waiting recv claim (%d) %s\n", ret, fi_strerror(ret));
            msg.error = ret;
            return msg;
        }
    }

    debug_info("[LFI] request " << request);
    debug_info("[LFI] End = " << size);
    return request;
}
}  // namespace LFI