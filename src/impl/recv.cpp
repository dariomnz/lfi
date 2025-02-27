
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

lfi_msg LFI::recv_internal(uint32_t comm_id, void *ptr, size_t size, recv_type type, uint32_t tag) {
    lfi_msg msg = {};
    int ret = 0;
    debug_info("[LFI] Start");

    // Check if comm exists
    std::shared_ptr<lfi_comm> comm = get_comm(comm_id);
    if (!comm) {
        msg.error = -LFI_COMM_NOT_FOUND;
        return msg;
    }
    lfi_request request(comm);

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

std::pair<lfi_msg, lfi_msg> LFI::any_recv(void *buffer_shm, void *buffer_peer, size_t size, uint32_t tag) {
    int ret = 0;
    lfi_msg peer_msg = {.error = -LFI_ERROR}, shm_msg = {.error = -LFI_ERROR};
    debug_info("[LFI] Start");

    // For the shm
    std::shared_ptr<lfi_comm> comm = get_comm(ANY_COMM_SHM);
    if (!comm) {
        throw std::runtime_error("There are no LFI_ANY_COMM_SHM. This should not happend");
    }
    lfi_request shm_request(comm);
    // Try a recv in shm
    ret = async_recv(buffer_shm, size, tag, shm_request);
    if (ret < 0) {
        shm_msg.error = ret;
        return {shm_msg, peer_msg};
    }
    // For the peer
    comm = get_comm(ANY_COMM_PEER);
    if (!comm) {
        throw std::runtime_error("There are no LFI_ANY_COMM_PEER. This should not happend");
    }
    lfi_request peer_request(comm);
    // Try a recv in peer
    ret = async_recv(buffer_peer, size, tag, peer_request);
    if (ret < 0) {
        peer_msg.error = ret;
        return {shm_msg, peer_msg};
    }

    bool finish = false;
    while (!finish) {
        ret = wait(shm_request, 0);
        if (ret != -LFI_TIMEOUT) {
            // it can be succesfully completed in the cancel
            cancel(peer_request);
            break;
        }

        ret = wait(peer_request, 0);
        if (ret != -LFI_TIMEOUT) {
            // it can be succesfully completed in the cancel
            cancel(shm_request);
            break;
        }
    }

    debug_info("[LFI] End shm_msg " << shm_msg.to_string() << " peer_msg " << peer_msg.to_string());
    return {shm_request, peer_request};
}

// any_recv with peek msg
// lfi_msg LFI::any_recv(void *buffer, size_t size, uint32_t tag)
// {
//     lfi_msg msg = {};
//     debug_info("[LFI] Start");

//     bool finish = false;
//     while(!finish){
//         // Try a recv in peer
//         msg = recv_peek(LFI_ANY_COMM_PEER, buffer, size, tag);
//         if (msg.error != -LFI_PEEK_NO_MSG){
//             break;
//         }
//         // Try a recv in shm
//         msg = recv_peek(LFI_ANY_COMM_SHM, buffer, size, tag);
//         if (msg.error != -LFI_PEEK_NO_MSG){
//             break;
//         }
//     }

//     debug_info("[LFI] End");
//     return msg;
// }

// any_recv with posting the buffer checking and canceling in loop
// lfi_msg LFI::any_recv(void *buffer, size_t size, uint32_t tag)
// {
//     lfi_msg peer_msg = {}, shm_msg = {};
//     debug_info("[LFI] Start");

//     // For the shm
//     lfi_comm *comm = get_comm(LFI_ANY_COMM_SHM);
//     if (comm == nullptr){
//         throw std::runtime_error("There are no LFI_ANY_COMM_SHM. This should not happend");
//     }
//     lfi_request shm_request(*comm);
//     // For the peer
//     comm = get_comm(LFI_ANY_COMM_PEER);
//     if (comm == nullptr){
//         throw std::runtime_error("There are no LFI_ANY_COMM_PEER. This should not happend");
//     }
//     lfi_request peer_request(*comm);

//     lfi_request* request = nullptr;
//     bool finish = false;
//     int ret = 0;
//     while(!finish){
//         // Try a recv in shm
//         shm_msg = async_recv(buffer, size, tag, shm_request);
//         if (shm_msg.error < 0){
//             return shm_msg;
//         }
//         ret = wait(shm_request, 0);
//         if (ret != -LFI_TIMEOUT){
//             request = &shm_request;
//             break;
//         }
//         // it can be succesfully completed in the cancel
//         ret = cancel(shm_request);
//         if (ret < 0 || shm_request.error == 0){
//             request = &shm_request;
//             break;
//         }

//         // Try a recv in peer
//         peer_msg = async_recv(buffer, size, tag, peer_request);
//         if (peer_msg.error < 0){
//             return peer_msg;
//         }
//         ret = wait(peer_request, 0);
//         if (ret != -LFI_TIMEOUT){
//             request = &peer_request;
//             break;
//         }
//         // it can be succesfully completed in the cancel
//         ret = cancel(peer_request);
//         if (ret < 0 || peer_request.error == 0){
//             request = &peer_request;
//             break;
//         }
//     }

//     lfi_msg msg;
//     msg.error = request->error;
//     msg.size = request->entry.len;
//     msg.tag = request->entry.tag & 0x0000'0000'0000'FFFF;
//     msg.rank_self_in_peer = (request->entry.tag & 0xFFFF'FF00'0000'0000) >> 40;
//     msg.rank = (request->entry.tag & 0x0000'00FF'FFFF'0000) >> 16;
//     debug_info("[LFI] End");
//     return msg;
// }

int LFI::async_recv_internal(void *buffer, size_t size, recv_type type, uint32_t tag, lfi_request &request,
                             int32_t timeout_ms) {
    int ret;

    // Check cancelled comm
    if (request.m_comm->is_canceled) {
        return -LFI_CANCELED_COMM;
    }

    std::unique_lock req_lock(request.mutex);
    request.reset();
    uint64_t mask = 0;
    decltype(std::chrono::high_resolution_clock::now()) start;
    if (timeout_ms >= 0) {
        start = std::chrono::high_resolution_clock::now();
    }

    uint64_t aux_rank_peer = request.m_comm->rank_peer;
    uint64_t aux_tag = tag;
    uint64_t tag_recv = (aux_rank_peer << MASK_RANK_BYTES) | aux_tag;

    if (request.m_comm->rank_peer == ANY_COMM_SHM || request.m_comm->rank_peer == ANY_COMM_PEER) {
        mask = MASK_RANK;
    }

    debug_info("[LFI] Start size " << size << " rank_peer " << request.m_comm->rank_peer << " rank_self_in_peer "
                                   << request.m_comm->rank_self_in_peer << " tag " << tag << " recv_context "
                                   << (void *)&request.context);

    fid_ep *p_rx_ep = request.m_comm->m_ep.use_scalable_ep ? request.m_comm->m_ep.rx_ep : request.m_comm->m_ep.ep;
    request.wait_context = true;
    request.is_send = false;
    do {
        if (type == recv_type::RECV) {
            ret = fi_trecv(p_rx_ep, buffer, size, NULL, request.m_comm->fi_addr, tag_recv, mask, &request.context);
        } else if (type == recv_type::RECVV) {
            ret = fi_trecvv(p_rx_ep, reinterpret_cast<const iovec *>(buffer), NULL, size, request.m_comm->fi_addr,
                            tag_recv, mask, &request.context);
        } else {
            std::runtime_error("Error unknown recv_type. This should not happend");
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
                return -LFI_CANCELED_COMM;
            }
        }
    } while (ret == -FI_EAGAIN);

    if (ret != 0) {
        printf("error posting recv buffer (%d)\n", ret);
        return -LFI_LIBFABRIC_ERROR;
    }

    debug_info("[LFI] Waiting on rank_peer " << request.m_comm->rank_peer);

    if (env::get_instance().LFI_fault_tolerance) {
        std::unique_lock ft_lock(request.m_comm->ft_mutex);
        debug_info("[LFI] insert request " << std::hex << &request << std::dec << " in comm "
                                           << request.m_comm->rank_peer);
        request.m_comm->ft_requests.insert(&request);
    }

    request.size = size;
    request.tag = tag;
    request.source = request.m_comm->rank_peer;

    debug_info("[LFI] msg size " << request.size << " source " << request.source << " tag " << request.tag << " error "
                                 << request.error);
    debug_info("[LFI] End = " << size);
    return LFI_SUCCESS;
}

lfi_msg LFI::recv_peek(uint32_t comm_id, void *buffer, size_t size, uint32_t tag) {
    int ret;
    lfi_msg msg = {};

    // Check if comm exists
    std::shared_ptr<lfi_comm> comm = get_comm(comm_id);
    if (!comm) {
        msg.error = -LFI_COMM_NOT_FOUND;
        return msg;
    }
    lfi_request request(comm);

    // Check cancelled comm
    if (request.m_comm->is_canceled) {
        msg.error = -LFI_CANCELED_COMM;
        return msg;
    }

    uint64_t mask = 0;

    uint64_t aux_rank_peer = request.m_comm->rank_peer;
    uint64_t aux_tag = tag;
    uint64_t tag_recv = (aux_rank_peer << MASK_RANK_BYTES) | aux_tag;

    if (request.m_comm->rank_peer == ANY_COMM_SHM || request.m_comm->rank_peer == ANY_COMM_PEER) {
        mask = MASK_RANK;
    }

    debug_info("[LFI] Start size " << size << " rank_peer " << request.m_comm->rank_peer << " rank_self_in_peer "
                                   << request.m_comm->rank_self_in_peer << " tag " << tag << " recv_context "
                                   << (void *)&request.context);

    fid_ep *p_rx_ep = request.m_comm->m_ep.use_scalable_ep ? request.m_comm->m_ep.rx_ep : request.m_comm->m_ep.ep;
    request.reset();
    request.is_send = false;
    iovec iov = {
        .iov_base = buffer,
        .iov_len = size,
    };
    fi_msg_tagged msg_to_peek = {
        .msg_iov = &iov,
        .desc = nullptr,
        .iov_count = 1,
        .addr = request.m_comm->fi_addr,
        .tag = tag_recv,
        .ignore = mask,
        .context = &request.context,
        .data = 0,
    };
    // First we PEEK with CLAIM to only generate one match
    do {
        ret = fi_trecvmsg(p_rx_ep, &msg_to_peek, FI_PEEK | FI_CLAIM);

        if (ret == -FI_EAGAIN) {
            protected_progress(request.m_comm->m_ep);

            if (request.m_comm->is_canceled) {
                msg.error = -LFI_CANCELED_COMM;
                return msg;
            }
        }
    } while (ret == -FI_EAGAIN);

    if (ret != 0) {
        printf("error PEEK recv buffer (%d)\n", ret);
        msg.error = -LFI_LIBFABRIC_ERROR;
        return msg;
    }

    debug_info("[LFI] Waiting for " << request.to_string());

    ret = wait(request);
    if (ret != 0) {
        printf("error waiting recv peek (%d)\n", ret);
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
                protected_progress(request.m_comm->m_ep);

                if (request.m_comm->is_canceled) {
                    msg.error = -LFI_CANCELED_COMM;
                    return msg;
                }
            }
        } while (ret == -FI_EAGAIN);

        if (ret != 0) {
            printf("error CLAIM recv buffer (%d)\n", ret);
            msg.error = -LFI_LIBFABRIC_ERROR;
            return msg;
        }

        debug_info("[LFI] Waiting for " << request.to_string());
        ret = wait(request);
        if (ret != 0) {
            printf("error waiting recv claim (%d)\n", ret);
            msg.error = ret;
            return msg;
        }
    }

    debug_info("[LFI] request size " << request.size << " source " << request.source << " tag " << request.tag
                                     << " error " << request.error);
    debug_info("[LFI] End = " << size);
    return request;
}
}  // namespace LFI