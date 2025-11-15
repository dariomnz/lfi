
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

inline bool LFI::wait_check_timeout(int32_t timeout_ms, decltype(std::chrono::high_resolution_clock::now()) start) {
    if (timeout_ms < 0) return false;
    int32_t elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start)
            .count();
    // debug_info("[LFI] Check timeout of "<<timeout_ms<<" ms with elapsed "<<elapsed_ms<<" ms")
    return elapsed_ms >= timeout_ms;
}

void LFI::wake_up_requests(lfi_endpoint &ep) {
    LFI_PROFILE_FUNCTION();
    if (!ep.in_progress.load()) {
        std::unique_lock lock(ep.waiting_requests_mutex);
        for (auto &var : ep.waiting_requests) {
            // Notify the threads
            if (auto req = std::get_if<lfi_request *>(&var)) {
                (*req)->cv.notify_all();
            } else if (auto w_struct = std::get_if<wait_struct *>(&var)) {
                (*w_struct)->wait_cv.notify_all();
            }
            // If progress is made one thread has to be running the progress so no more threads are necesary to wake
            // up
            if (ep.in_progress.load()) {
                break;
            }
        }
        // Wake the ping pong thread
        if (ep.waiting_requests.empty()) {
            ft_cv.notify_one();
        }
    }
}

int LFI::wait(lfi_request &request, int32_t timeout_ms) {
    LFI_PROFILE_FUNCTION();
    debug_info("[LFI] Start timeout_ms " << timeout_ms);

    // Check cancelled comm
    // if (request.m_comm.is_canceled) {
    //     return -LFI_BROKEN_COMM;
    // }

    lfi_endpoint &ep = request.m_comm.m_ep;
    decltype(std::chrono::high_resolution_clock::now()) start;
    bool is_timeout = false;
    if (timeout_ms >= 0) {
        start = std::chrono::high_resolution_clock::now();
    }

    {
        std::unique_lock lock(ep.waiting_requests_mutex);
        ep.waiting_requests.emplace(&request);
    }

    {
        std::unique_lock request_lock(request.mutex);
        debug_info("[LFI] " << request);
        while (request.wait_context && !is_timeout) {
            if (!env::get_instance().LFI_efficient_progress || !ep.in_progress.exchange(true)) {
                while (request.wait_context && !is_timeout) {
                    request_lock.unlock();
                    ep.progress();
                    request_lock.lock();
                    is_timeout = wait_check_timeout(timeout_ms, start);
                }
                ep.in_progress.store(false);
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
        std::unique_lock lock(ep.waiting_requests_mutex);
        ep.waiting_requests.erase(&request);
    }
    wake_up_requests(ep);

    // Return timeout only if is not completed and timeout
    if (is_timeout && request.wait_context) {
        request.error = -LFI_TIMEOUT;
        debug_info("[LFI] End wait with timeout " << request);
        return -LFI_TIMEOUT;
    }

    debug_info("[LFI] End wait " << request);
    return LFI_SUCCESS;
}

int LFI::wait_num(lfi_request **requests, int n_requests, int how_many, int32_t timeout_ms) {
    LFI_PROFILE_FUNCTION();
    debug_info("[LFI] Start how_many " << how_many << " timeout_ms " << timeout_ms);
    if (how_many > n_requests || how_many <= 0 || n_requests == 0) return -1;

    // If only one redirect to wait
    if (how_many == 1 && n_requests == 1) {
        return wait(*requests[0], timeout_ms);
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
    for (int i = 0; i < n_requests; i++) {
        auto &request = *requests[i];
        std::scoped_lock req_and_shared_wait_lock(request.mutex, shared_wait.wait_mutex);
        request.shared_wait_struct = &shared_wait;
        debug_info(request);
        debug_info("Request comm " << request.m_comm.rank_peer);
        if (request.is_completed()) {
            debug_info("Request already completed");
            shared_wait.wait_count--;
        } else {
            if (request.error == -LFI_TIMEOUT) {
                request.error = LFI_SUCCESS;
            }
        }
        if (request.m_comm.m_ep == shm_ep) {
            wait_shm = true;
        } else if (request.m_comm.m_ep == peer_ep) {
            wait_peer = true;
        }
    }

    int temp_wait_count;
    {
        std::unique_lock wait_lock(shared_wait.wait_mutex);
        temp_wait_count = shared_wait.wait_count;
    }
    if (temp_wait_count > 0) {
        if (wait_shm) {
            std::unique_lock lock(shm_ep.waiting_requests_mutex);
            shm_ep.waiting_requests.emplace(&shared_wait);
        }
        if (wait_peer) {
            std::unique_lock lock(peer_ep.waiting_requests_mutex);
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
                    made_progress = shm_ep.protected_progress();
                }
                if (wait_peer) {
                    made_progress = peer_ep.protected_progress();
                }
                wait_lock.lock();
                if (!made_progress && shared_wait.wait_count > 0) {
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
                std::unique_lock lock(shm_ep.waiting_requests_mutex);
                shm_ep.waiting_requests.erase(&shared_wait);
            }
            wake_up_requests(shm_ep);
        }
        if (wait_peer) {
            {
                std::unique_lock lock(peer_ep.waiting_requests_mutex);
                peer_ep.waiting_requests.erase(&shared_wait);
            }
            wake_up_requests(peer_ep);
        }
    }

    int out_index = -1;
    static uint32_t linear_counter = 0;
    int first_rand = linear_counter++ % n_requests;
    int index = 0;
    for (int i = 0; i < n_requests; i++) {
        index = (first_rand + i) % n_requests;
        auto &request = *requests[index];
        {
            std::unique_lock request_lock(request.mutex);
            request.shared_wait_struct = nullptr;
            // Get the index in the vector for the first completed
            if (request.is_completed()) {
                if (out_index == -1) {
                    out_index = index;
                }
            } else if (request.error == LFI_SUCCESS) {
                debug_info("[LFI] timeout a request in wait_num " << request);
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