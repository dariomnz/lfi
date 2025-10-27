
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

inline std::string fi_flags_to_string(uint64_t flags) {
    std::stringstream out;
    if (flags == 0) out << "    NONE" << std::endl;
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

inline std::string fi_cq_tagged_entry_to_string(const fi_cq_tagged_entry &entry) {
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
        // out << "    real_tag: " << (entry.tag & MASK_TAG) << std::endl;
        out << "    real_tag: " << lfi_tag_to_string(entry.tag & MASK_TAG) << std::endl;
        out << "    rank: " << ((entry.tag & MASK_RANK) >> MASK_RANK_BYTES) << std::endl;
    }
    return out.str();
}

inline std::string fi_cq_err_entry_to_string(const fi_cq_err_entry &entry, fid_cq *cq) {
    std::stringstream out;
    out << "fi_cq_err_entry:" << std::endl;
    out << "  op_context: " << entry.op_context << std::endl;
    out << "  Flags set:" << std::endl;
    out << fi_flags_to_string(entry.flags);
    out << "  len: " << entry.len << std::endl;
    out << "  buf: " << entry.buf << std::endl;
    out << "  data: " << entry.data << std::endl;
    out << "  tag: " << entry.tag << std::endl;
    out << "    real_tag: " << (entry.tag & MASK_TAG) << std::endl;
    out << "    rank: " << ((entry.tag & MASK_RANK) >> MASK_RANK_BYTES) << std::endl;
    out << "  olen: " << entry.olen << std::endl;
    out << "  err: " << entry.err << " " << fi_strerror(entry.err) << std::endl;
    out << "  prov_errno: " << entry.prov_errno << " " << fi_cq_strerror(cq, entry.prov_errno, entry.err_data, NULL, 0)
        << std::endl;
    out << "  err_data: " << entry.err_data << std::endl;
    out << "  err_data_size: " << entry.err_data_size << std::endl;
    return out.str();
}

void lfi_request::complete() {
    debug_info("[LFI] >> Begin complete request " << to_string());
    std::unique_lock request_lock(mutex);
    wait_context = false;
    cv.notify_all();
    if (shared_wait_struct != nullptr) {
        std::unique_lock shared_wait_lock(shared_wait_struct->wait_mutex);
        debug_info("[LFI] have shared_wait_struct");
        shared_wait_struct->wait_count--;
        if (shared_wait_struct->wait_count <= 0) {
            shared_wait_struct->wait_cv.notify_all();
        }
    }
    if (env::get_instance().LFI_fault_tolerance) {
        std::unique_lock ft_lock(m_comm->ft_mutex);
        debug_info("[LFI] erase request " << to_string() << " in comm " << m_comm->rank_peer);
        m_comm->ft_requests.erase(this);

        if (m_comm->rank_peer != ANY_COMM_SHM && m_comm->rank_peer != ANY_COMM_PEER) {
            std::unique_lock lock(m_comm->m_ep.requests_mutex);
            m_comm->ft_comm_count > 0 ? m_comm->ft_comm_count-- : 0;
            debug_info("[LFI] ft_comm_count " << m_comm->ft_comm_count);
            if (m_comm->ft_comm_count == 0) {
                m_comm->m_ep.ft_comms.erase(m_comm);
            }
        } else {
            std::unique_lock lock(m_comm->m_ep.requests_mutex);
            debug_info("[LFI] remove of ft_any_comm_requests " << to_string());
            m_comm->m_ep.ft_any_comm_requests.erase(this);
        }
    }
    if (callback) {
        // Call the callback with the current error of the request
        debug_info("[LFI] calling callback on complete for request " << to_string());
        request_lock.unlock();
        callback(error);
        // Maybe the callback clean the request
    } else {
        debug_info("[LFI] there are no callback to call");
        debug_info("[LFI] << End complete request " << to_string());
    }
}

std::string lfi_request::to_string() {
    std::unique_lock lock(mutex);
    std::stringstream out;
    out << "Request " << std::hex << this;
    out << std::dec << " comm " << m_comm->rank_peer << " {size:" << size << ", tag:" << lfi_tag_to_string(tag)
        << ", source:" << lfi_source_to_string(source) << "}";
    if (is_send) {
        out << " is_send";
    }
    if (is_inject) {
        out << " is_inject";
    }
    if (is_completed()) {
        out << " is_completed";
    }
    if (shared_wait_struct) {
        out << " shared_wait";
    }
    if (error) {
        out << " Error: " << lfi_strerror(error);
    }
    return out.str();
}

int LFI::progress(lfi_ep &lfi_ep) {
    int ret;
    const int comp_count = 8;
    struct fi_cq_tagged_entry comp[comp_count] = {};

    // Libfabric progress
    ret = fi_cq_read(lfi_ep.cq, comp, comp_count);
    if (ret == -FI_EAGAIN) {
        ret = LFI_SUCCESS;
    } else if (ret == -FI_EAVAIL) {
        debug_info("[Error] fi_cq_read " << ret << " " << fi_strerror(ret));
        fi_cq_err_entry err;
        fi_cq_readerr(lfi_ep.cq, &err, 0);
        debug_info(fi_cq_err_entry_to_string(err, lfi_ep.cq));

        if (std::abs(err.err) == FI_ECANCELED) {
            return ret;
        } else {
            lfi_request *request_p = static_cast<lfi_request *>(err.op_context);
            {
                std::unique_lock request_lock(request_p->mutex);
                debug_info("[Error] " << request_p->to_string() << " cq error");
                if (std::abs(err.err) == FI_ECANCELED) {
                    request_p->error = -LFI_CANCELED;
                } else if (std::abs(err.err) == FI_ENOMSG) {
                    request_p->error = -LFI_PEEK_NO_MSG;
                } else {
                    request_p->error = -LFI_LIBFABRIC_ERROR;
                }
            }
            request_p->complete();
        }
    } else if (ret > 0) {
        // Handle the cq entries
        for (int i = 0; i < ret; i++) {
            lfi_request *request = static_cast<lfi_request *>(comp[i].op_context);
            debug_info(fi_cq_tagged_entry_to_string(comp[i]));
            {
                std::unique_lock request_lock(request->mutex);
                // If len is not 0 is a RECV
                if (!request->is_send) {
                    request->size = comp[i].len;
                    request->tag = (comp[i].tag & MASK_TAG);
                    request->source = ((comp[i].tag & MASK_RANK) >> MASK_RANK_BYTES);

                    if (env::get_instance().LFI_fault_tolerance) {
                        // Update time
                        if (request->m_comm->rank_peer == ANY_COMM_SHM || request->m_comm->rank_peer == ANY_COMM_PEER) {
                            auto comm = get_comm(request->source);
                            if (comm) {
                                comm->update_request_time();
                            }
                        } else {
                            request->m_comm->update_request_time();
                        }
                    }
                }
                request->error = LFI_SUCCESS;
                debug_info("Completed: " << request->to_string());
            }
            request->complete();
        }
    }

    if (env::get_instance().LFI_fault_tolerance) {
        static auto last_ft_execution_time = std::chrono::high_resolution_clock::now();
        static std::mutex exclusive_mutex{};
        std::unique_lock lock(exclusive_mutex, std::defer_lock);
        if (lock.try_lock()) {
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed_time =
                std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_ft_execution_time);

            if (elapsed_time > std::chrono::milliseconds(10)) {
                // debug_info("[LFI] runing ft");
                ft_one_loop(lfi_ep);
                last_ft_execution_time = current_time;
            }
        }
    }

    return ret;
}

inline bool LFI::wait_check_timeout(int32_t timeout_ms, decltype(std::chrono::high_resolution_clock::now()) start) {
    if (timeout_ms < 0) return false;
    int32_t elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start)
            .count();
    // debug_info("[LFI] Check timeout of "<<timeout_ms<<" ms with elapsed "<<elapsed_ms<<" ms")
    return elapsed_ms >= timeout_ms;
}

void LFI::wake_up_requests(lfi_ep &ep) {
    if (!ep.progress.load()) {
        std::unique_lock lock(ep.requests_mutex);
        for (auto &var : ep.waiting_requests) {
            // Notify the threads
            if (auto req = std::get_if<lfi_request *>(&var)) {
                (*req)->cv.notify_all();
            } else if (auto w_struct = std::get_if<wait_struct *>(&var)) {
                (*w_struct)->wait_cv.notify_all();
            }
            // If progress is made one thread has to be running the progress so no more threads are necesary to wake
            // up
            if (ep.progress.load()) {
                break;
            }
        }
        // Wake the ping pong thread
        if (ep.waiting_requests.empty()) {
            ft_cv.notify_one();
        }
    }
}

bool LFI::protected_progress(lfi_ep &ep) {
    bool made_progress = false;
    if (!env::get_instance().LFI_efficient_progress || !ep.progress.exchange(true)) {
        // debug_info("Run progress from protected_progress");
        progress(ep);
        ep.progress.store(false);
        made_progress = true;
    }
    return made_progress;
}

int LFI::wait(lfi_request &request, int32_t timeout_ms) {
    debug_info("[LFI] Start " << request.to_string() << " timeout_ms " << timeout_ms);

    // Check cancelled comm
    if (request.m_comm->is_canceled) {
        return -LFI_BROKEN_COMM;
    }

    lfi_ep &ep = request.m_comm->m_ep;
    decltype(std::chrono::high_resolution_clock::now()) start;
    bool is_timeout = false;
    if (timeout_ms >= 0) {
        start = std::chrono::high_resolution_clock::now();
    }

    {
        std::unique_lock lock(ep.requests_mutex);
        ep.waiting_requests.emplace(&request);
    }

    {
        std::unique_lock request_lock(request.mutex);
        while (request.wait_context && !is_timeout) {
            if (!env::get_instance().LFI_efficient_progress || !ep.progress.exchange(true)) {
                while (request.wait_context && !is_timeout) {
                    request_lock.unlock();
                    progress(ep);
                    request_lock.lock();
                    is_timeout = wait_check_timeout(timeout_ms, start);
                }
                ep.progress.store(false);
            } else {
                if (timeout_ms >= 0) {
                    int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::high_resolution_clock::now() - start)
                                             .count();
                    request.cv.wait_for(request_lock, std::chrono::milliseconds(timeout_ms - elapsed_ms));
                } else {
                    request.cv.wait(request_lock);
                }
            }
            is_timeout = wait_check_timeout(timeout_ms, start);
        }
    }

    {
        std::unique_lock lock(ep.requests_mutex);
        ep.waiting_requests.erase(&request);
    }
    wake_up_requests(ep);

    // Return timeout only if is not completed and timeout
    if (is_timeout && request.wait_context) {
        request.error = -LFI_TIMEOUT;
        debug_info("[LFI] End wait with timeout " << request.to_string());
        return -LFI_TIMEOUT;
    }

    debug_info("[LFI] End wait " << request.to_string());
    return LFI_SUCCESS;
}

int LFI::wait_num(std::vector<std::reference_wrapper<lfi_request>> &requests, int how_many, int32_t timeout_ms) {
    debug_info("[LFI] Start how_many " << how_many << " timeout_ms " << timeout_ms);
    if (how_many > static_cast<int>(requests.size()) || how_many <= 0 || requests.size() == 0) return -1;

    // If only one redirect to wait
    if (how_many == 1 && requests.size() == 1) {
        return wait(requests[0], timeout_ms);
    }

    wait_struct shared_wait = {.wait_count = how_many};
    decltype(std::chrono::high_resolution_clock::now()) start;
    bool is_timeout = false;
    bool wait_shm = false;
    bool wait_peer = false;
    if (timeout_ms >= 0) {
        start = std::chrono::high_resolution_clock::now();
    }
    // Set up the wait
    for (auto &request_ref : requests) {
        auto &request = request_ref.get();
        std::scoped_lock req_and_shared_wait_lock(request.mutex, shared_wait.wait_mutex);
        request.shared_wait_struct = &shared_wait;
        debug_info(request.to_string());
        debug_info("Request comm " << request.m_comm->rank_peer);
        if (request.is_completed()) {
            debug_info("Request already completed");
            shared_wait.wait_count--;
        }
        if (request.m_comm->m_ep == shm_ep) {
            wait_shm = true;
        } else if (request.m_comm->m_ep == peer_ep) {
            wait_peer = true;
        }
    }
    if (shared_wait.wait_count > 0) {
        if (wait_shm) {
            std::unique_lock lock(shm_ep.requests_mutex);
            shm_ep.waiting_requests.emplace(&shared_wait);
        }
        if (wait_peer) {
            std::unique_lock lock(peer_ep.requests_mutex);
            peer_ep.waiting_requests.emplace(&shared_wait);
        }

        {
            std::unique_lock wait_lock(shared_wait.wait_mutex);
            bool made_progress = false;
            while (shared_wait.wait_count > 0 && !is_timeout) {
                // debug_info("shared_wait.wait_count "<<shared_wait.wait_count);
                made_progress = false;
                wait_lock.unlock();
                if (wait_shm) {
                    made_progress = protected_progress(shm_ep);
                }
                if (wait_peer) {
                    made_progress = protected_progress(peer_ep);
                }
                wait_lock.lock();
                if (!made_progress) {
                    if (timeout_ms >= 0) {
                        int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 std::chrono::high_resolution_clock::now() - start)
                                                 .count();
                        shared_wait.wait_cv.wait_for(wait_lock, std::chrono::milliseconds(timeout_ms - elapsed_ms));
                    } else {
                        shared_wait.wait_cv.wait(wait_lock);
                    }
                }
                is_timeout = wait_check_timeout(timeout_ms, start);
            }
        }

        // Clean wait
        if (wait_shm) {
            {
                std::unique_lock lock(shm_ep.requests_mutex);
                shm_ep.waiting_requests.erase(&shared_wait);
            }
            wake_up_requests(shm_ep);
        }
        if (wait_peer) {
            {
                std::unique_lock lock(peer_ep.requests_mutex);
                peer_ep.waiting_requests.erase(&shared_wait);
            }
            wake_up_requests(peer_ep);
        }
    }

    int out_index = -1;
    int first_rand = rand() % requests.size();
    int index = 0;
    for (int i = 0; i < static_cast<int>(requests.size()); i++) {
        index = (first_rand + i) % requests.size();
        auto &request = requests[index].get();
        {
            std::unique_lock request_lock(request.mutex);
            request.shared_wait_struct = nullptr;
            // Get the index in the vector for the first completed
            if (request.is_completed()) {
                if (out_index == -1) {
                    out_index = index;
                }
            } else {
                request.error = -LFI_TIMEOUT;
            }
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