
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

static inline std::string fi_flags_to_string(uint64_t flags) {
    std::stringstream out;
    if (flags & FI_MSG) out << "    FI_MSG" << std::endl;
    if (flags & FI_RMA) out << "    FI_RMA" << std::endl;
    if (flags & FI_TAGGED) out << "    FI_TAGGED" << std::endl;
    if (flags & FI_ATOMIC) out << "    FI_ATOMIC" << std::endl;
    if (flags & FI_MULTICAST) out << "    FI_MULTICAST" << std::endl;
    if (flags & FI_COLLECTIVE) out << "    FI_COLLECTIVE" << std::endl;

    if (flags & FI_READ) out << "    FI_READ" << std::endl;
    if (flags & FI_WRITE) out << "    FI_WRITE" << std::endl;
    if (flags & FI_RECV) out << "    FI_RECV" << std::endl;
    if (flags & FI_SEND) out << "    FI_SEND" << std::endl;
    if (flags & FI_REMOTE_READ) out << "    FI_REMOTE_READ" << std::endl;
    if (flags & FI_REMOTE_WRITE) out << "    FI_REMOTE_WRITE" << std::endl;

    if (flags & FI_MULTI_RECV) out << "    FI_MULTI_RECV" << std::endl;
    if (flags & FI_REMOTE_CQ_DATA) out << "    FI_REMOTE_CQ_DATA" << std::endl;
    if (flags & FI_MORE) out << "    FI_MORE" << std::endl;
    if (flags & FI_PEEK) out << "    FI_PEEK" << std::endl;
    if (flags & FI_TRIGGER) out << "    FI_TRIGGER" << std::endl;
    if (flags & FI_FENCE) out << "    FI_FENCE" << std::endl;
    // if (flags & FI_PRIORITY) out << "    FI_PRIORITY" << std::endl;

    if (flags & FI_COMPLETION) out << "    FI_COMPLETION" << std::endl;
    if (flags & FI_INJECT) out << "    FI_INJECT" << std::endl;
    if (flags & FI_INJECT_COMPLETE) out << "    FI_INJECT_COMPLETE" << std::endl;
    if (flags & FI_TRANSMIT_COMPLETE) out << "    FI_TRANSMIT_COMPLETE" << std::endl;
    if (flags & FI_DELIVERY_COMPLETE) out << "    FI_DELIVERY_COMPLETE" << std::endl;
    if (flags & FI_AFFINITY) out << "    FI_AFFINITY" << std::endl;
    if (flags & FI_COMMIT_COMPLETE) out << "    FI_COMMIT_COMPLETE" << std::endl;
    if (flags & FI_MATCH_COMPLETE) out << "    FI_MATCH_COMPLETE" << std::endl;

    if (flags & FI_HMEM) out << "    FI_HMEM" << std::endl;
    if (flags & FI_VARIABLE_MSG) out << "    FI_VARIABLE_MSG" << std::endl;
    if (flags & FI_RMA_PMEM) out << "    FI_RMA_PMEM" << std::endl;
    if (flags & FI_SOURCE_ERR) out << "    FI_SOURCE_ERR" << std::endl;
    if (flags & FI_LOCAL_COMM) out << "    FI_LOCAL_COMM" << std::endl;
    if (flags & FI_REMOTE_COMM) out << "    FI_REMOTE_COMM" << std::endl;
    if (flags & FI_SHARED_AV) out << "    FI_SHARED_AV" << std::endl;
    if (flags & FI_PROV_ATTR_ONLY) out << "    FI_PROV_ATTR_ONLY" << std::endl;
    if (flags & FI_NUMERICHOST) out << "    FI_NUMERICHOST" << std::endl;
    if (flags & FI_RMA_EVENT) out << "    FI_RMA_EVENT" << std::endl;
    if (flags & FI_SOURCE) out << "    FI_SOURCE" << std::endl;
    if (flags & FI_NAMED_RX_CTX) out << "    FI_NAMED_RX_CTX" << std::endl;
    if (flags & FI_DIRECTED_RECV) out << "    FI_DIRECTED_RECV" << std::endl;
    return out.str();
}

static inline std::string fi_cq_tagged_entry_to_string(const fi_cq_tagged_entry &entry) {
    std::stringstream out;
    out << "fi_cq_tagged_entry:" << std::endl;
    out << "  op_context: " << entry.op_context << std::endl;
    out << "  Flags set:" << std::endl;
    out << fi_flags_to_string(entry.flags);
    out << "  len: " << entry.len << std::endl;
    out << "  buf: " << entry.buf << std::endl;
    out << "  data: " << entry.data << std::endl;
    out << "  tag: " << entry.tag << std::endl;
    if (entry.flags & FI_RECV) {
        out << "    real_tag: " << (entry.tag & 0x0000'0000'0000'FFFF) << std::endl;
        out << "    rank_peer: " << ((entry.tag & 0x0000'00FF'FFFF'0000) >> 16) << std::endl;
        out << "    rank_self_in_peer: " << ((entry.tag & 0xFFFF'FF00'0000'0000) >> 40) << std::endl;
    }
    return out.str();
}

static inline std::string fi_cq_err_entry_to_string(const fi_cq_err_entry &entry, fid_cq *cq) {
    std::stringstream out;
    out << "fi_cq_err_entry:" << std::endl;
    out << "  op_context: " << entry.op_context << std::endl;
    out << "  Flags set:" << std::endl;
    out << fi_flags_to_string(entry.flags);
    out << "  len: " << entry.len << std::endl;
    out << "  buf: " << entry.buf << std::endl;
    out << "  data: " << entry.data << std::endl;
    out << "  tag: " << entry.tag << std::endl;
    out << "    real_tag: " << (entry.tag & 0x0000'0000'0000'FFFF) << std::endl;
    out << "    rank_peer: " << ((entry.tag & 0x0000'00FF'FFFF'0000) >> 16) << std::endl;
    out << "    rank_self_in_peer: " << ((entry.tag & 0xFFFF'FF00'0000'0000) >> 40) << std::endl;
    out << "  olen: " << entry.olen << std::endl;
    out << "  err: " << entry.err << " " << fi_strerror(entry.err) << std::endl;
    out << "  prov_errno: " << entry.prov_errno << " " << fi_cq_strerror(cq, entry.prov_errno, entry.err_data, NULL, 0)
        << std::endl;
    out << "  err_data: " << entry.err_data << std::endl;
    out << "  err_data_size: " << entry.err_data_size << std::endl;
    return out.str();
}

int LFI::progress(lfi_ep &lfi_ep) {
    int ret;
    const int comp_count = 8;
    struct fi_cq_tagged_entry comp[comp_count] = {};

    // Libfabric progress
    ret = fi_cq_read(lfi_ep.cq, comp, comp_count);
    if (ret == -FI_EAGAIN) {
        return 0;
    }

    if (ret == -FI_EAVAIL) {
        debug_info("[Error] fi_cq_read " << ret << " " << fi_strerror(ret));
        fi_cq_err_entry err;
        fi_cq_readerr(lfi_ep.cq, &err, 0);
        debug_info(fi_cq_err_entry_to_string(err, lfi_ep.cq));

        lfi_request *request_p = static_cast<lfi_request *>(err.op_context);
        std::unique_lock request_lock(request_p->mutex);
        debug_info("[Error] " << request_p->to_string() << " cq error");
        request_p->wait_context = false;
        if (std::abs(err.err) == FI_ECANCELED) {
            request_p->error = -LFI_CANCELED;
        } else if (std::abs(err.err) == FI_ENOMSG) {
            request_p->error = -LFI_PEEK_NO_MSG;
        } else {
            request_p->error = -LFI_ERROR;
        }
        request_p->cv.notify_all();
        if (request_p->shared_wait_struct.has_value()) {
            auto &shared_wait_struct = request_p->shared_wait_struct.value().get();
            std::unique_lock shared_wait_lock(shared_wait_struct.wait_mutex);
            shared_wait_struct.wait_count--;
            if (shared_wait_struct.wait_count <= 0) {
                shared_wait_struct.wait_cv.notify_all();
            }
        }
        return ret;
    }

    // Handle the cq entries
    for (int i = 0; i < ret; i++) {
        lfi_request *request = static_cast<lfi_request *>(comp[i].op_context);
        debug_info(fi_cq_tagged_entry_to_string(comp[i]));

        std::unique_lock request_lock(request->mutex);
        request->entry = comp[i];
        request->error = LFI_SUCCESS;
        request->wait_context = false;
        request->cv.notify_all();
        if (request->shared_wait_struct.has_value()) {
            auto &shared_wait_struct = request->shared_wait_struct.value().get();
            std::unique_lock shared_wait_lock(shared_wait_struct.wait_mutex);
            shared_wait_struct.wait_count--;
            if (shared_wait_struct.wait_count <= 0) {
                shared_wait_struct.wait_cv.notify_all();
            }
        }
    }
    return ret;
}

bool LFI::wait_check_timeout(int32_t timeout_ms, decltype(std::chrono::high_resolution_clock::now()) start) {
    int32_t elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start)
            .count();
    // debug_info("[LFI] Check timeout of "<<timeout_ms<<" ms with elapsed "<<elapsed_ms<<" ms")
    if (elapsed_ms >= timeout_ms) {
        // int ret = cancel(request);
        // if (ret < 0){
        //     print("TODO: check error in fi_cancel");
        // }
        return true;
    }
    return false;
}

int LFI::wait(lfi_request &request, int32_t timeout_ms) {
    debug_info("[LFI] Start " << request.to_string() << " timeout_ms " << timeout_ms);

    // Check cancelled comm
    if (request.m_comm->is_canceled) {
        return -LFI_CANCELED;
    }

    std::unique_lock ep_lock(request.m_comm->m_ep.mutex_ep, std::defer_lock);
    std::unique_lock request_lock(request.mutex);
    decltype(std::chrono::high_resolution_clock::now()) start;
    bool is_timeout = false;
    if (timeout_ms >= 0) {
        start = std::chrono::high_resolution_clock::now();
    }

    while (request.wait_context && !is_timeout) {
        if (ep_lock.try_lock()) {
            request_lock.unlock();
            progress(request.m_comm->m_ep);
            request_lock.lock();
            ep_lock.unlock();
        } else {
            request.cv.wait_for(request_lock, std::chrono::milliseconds(env::get_instance().LFI_ms_wait_sleep));
        }
        if (timeout_ms >= 0) {
            is_timeout = wait_check_timeout(timeout_ms, start);
        }
    }
    request_lock.unlock();
    // Return timeout only if is not completed and timeout
    if (is_timeout && request.wait_context) {
        request.error = -LFI_TIMEOUT;
        debug_info("[LFI] End wait with timeout " << request.to_string());
        return -LFI_TIMEOUT;
    }

    if (env::get_instance().LFI_fault_tolerance) {
        std::unique_lock ft_lock(request.m_comm->ft_mutex);
        debug_info("[LFI] erase request " << request.to_string() << " in comm " << request.m_comm.rank_peer);
        request.m_comm->ft_requests.erase(&request);
    }

    debug_info("[LFI] End wait " << request.to_string());
    return LFI_SUCCESS;
}

int LFI::wait_num(std::vector<std::reference_wrapper<lfi_request>> &requests, int how_many, int32_t timeout_ms) {
    debug_info("[LFI] Start how_many " << how_many << " timeout_ms " << timeout_ms);
    if (how_many > static_cast<int>(requests.size()) || how_many <= 0 || requests.size() == 0) return -1;
    wait_struct shared_wait = {.wait_count = how_many};
    int wait_shm_ep = 0, wait_peer_ep = 0;
    std::optional<std::reference_wrapper<lfi_request>> one_shm_rq, one_peer_rq;
    decltype(std::chrono::high_resolution_clock::now()) start;
    bool is_timeout = false;
    if (timeout_ms >= 0) {
        start = std::chrono::high_resolution_clock::now();
    }
    // Set up the wait
    for (auto &request_ref : requests) {
        auto &request = request_ref.get();
        std::scoped_lock req_and_shared_wait_lock(request.mutex, shared_wait.wait_mutex);
        request.shared_wait_struct = shared_wait;
        debug_info(request.to_string());
        debug_info("Request comm " << request.m_comm->rank_peer);
        if (request.is_completed()) {
            debug_info("Request already completed");
            shared_wait.wait_count--;
        }
        if (request.m_comm->m_ep == shm_ep) {
            wait_shm_ep++;
            one_shm_rq = request_ref;
        } else if (request.m_comm->m_ep == peer_ep) {
            wait_peer_ep++;
            one_peer_rq = request_ref;
        }
    }

    if (wait_shm_ep > 0 && wait_peer_ep > 0) {
        shared_wait.wait_type = wait_endpoint::ALL;
    } else if (wait_shm_ep > 0 && wait_peer_ep == 0) {
        shared_wait.wait_type = wait_endpoint::SHM;
    } else if (wait_shm_ep == 0 && wait_peer_ep > 0) {
        shared_wait.wait_type = wait_endpoint::PEER;
    } else {
        debug_error("This should not happend!!");
        return -1;
    }

    std::unique_lock wait_lock(shared_wait.wait_mutex);
    std::unique_lock shm_lock(shm_ep.mutex_ep, std::defer_lock);
    std::unique_lock peer_lock(peer_ep.mutex_ep, std::defer_lock);
    bool made_progress = false;
    while (shared_wait.wait_count > 0 && !is_timeout) {
        // debug_info("shared_wait.wait_count "<<shared_wait.wait_count);
        made_progress = false;
        if (shared_wait.wait_type == wait_endpoint::ALL || shared_wait.wait_type == wait_endpoint::SHM) {
            if (shm_lock.try_lock()) {
                // debug_info("progress shm");
                wait_lock.unlock();
                progress(shm_ep);
                wait_lock.lock();
                shm_lock.unlock();
                made_progress = true;
            }
        }
        if (shared_wait.wait_type == wait_endpoint::ALL || shared_wait.wait_type == wait_endpoint::PEER) {
            if (peer_lock.try_lock()) {
                // debug_info("progress peer");
                wait_lock.unlock();
                progress(peer_ep);
                wait_lock.lock();
                peer_lock.unlock();
                made_progress = true;
            }
        }
        if (!made_progress) {
            // debug_info("wait");
            shared_wait.wait_cv.wait_for(wait_lock, std::chrono::milliseconds(env::get_instance().LFI_ms_wait_sleep));
        }
        if (timeout_ms >= 0) {
            is_timeout = wait_check_timeout(timeout_ms, start);
        }
    }

    wait_lock.unlock();

    // Clean wait
    int out_index = -1;
    int first_rand = rand() % requests.size();
    int index = 0;
    for (int i = 0; i < static_cast<int>(requests.size()); i++) {
        index = (first_rand + i) % requests.size();
        auto &request = requests[index].get();
        {
            std::scoped_lock request_lock(request.mutex);
            request.shared_wait_struct = {};
            // Get the int the vector first completed
            if (request.is_completed()) {
                if (out_index == -1) {
                    out_index = index;
                }
            } else {
                request.error = -LFI_TIMEOUT;
            }
        }

        if (env::get_instance().LFI_fault_tolerance) {
            std::unique_lock ft_lock(request.m_comm->ft_mutex);
            debug_info("[LFI] erase request " << request.to_string() << " in comm " << request.m_comm->rank_peer);
            request.m_comm->ft_requests.erase(&request);
        }
    }

    if (is_timeout) {
        debug_info("[LFI] End how_many " << how_many << " timeout");
        return -LFI_TIMEOUT;
    }
    debug_info("[LFI] End how_many " << how_many << " out_index " << out_index);
    return out_index;
}
}  // namespace LFI