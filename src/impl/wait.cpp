
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
#include "impl/helpers.hpp"
#include "impl/lfi.hpp"
#include "impl/profiler.hpp"
#include "lfi_error.h"

namespace LFI {

inline bool LFI::wait_check_timeout(int32_t timeout_ms, decltype(std::chrono::high_resolution_clock::now()) start) {
    if (timeout_ms < 0) return false;
    int32_t elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start)
            .count();
    // debug_info("[LFI] Check timeout of "<<timeout_ms<<" ms with elapsed "<<elapsed_ms<<" ms")
    return elapsed_ms >= timeout_ms;
}

int LFI::test(lfi_request &request) {
    std::unique_lock request_lock(request.mutex);
    auto &endpoint = request.m_endpoint;
    ProgressGuard progress_guard(endpoint);
    if (progress_guard.is_leader()) {
        request_lock.unlock();
        endpoint.progress(true);
        request_lock.lock();
    }
    if (request.is_completed()) {
        return LFI_SUCCESS;
    } else {
        return -LFI_TIMEOUT;
    }
}

int LFI::wait(lfi_request &request, int32_t timeout_ms) {
    LFI_PROFILE_FUNCTION();
    debug_info("[LFI] Start timeout_ms " << timeout_ms);

    // Check cancelled comm
    // if (request.m_comm.is_canceled) {
    //     return -LFI_BROKEN_COMM;
    // }

    lfi_endpoint &ep = request.m_endpoint;
    decltype(std::chrono::high_resolution_clock::now()) start;
    bool is_timeout = false;
    if (timeout_ms >= 0) {
        start = std::chrono::high_resolution_clock::now();
    }

    {
        std::unique_lock request_lock(request.mutex);
        debug_info("[LFI] " << request);
        while (!request.is_completed() && !is_timeout) {
            ProgressGuard guard(ep, &request.cv);
            if (guard.is_leader()) {
                request_lock.unlock();
                while (!request.is_completed() && !is_timeout) {
                    ep.progress(true);
                    is_timeout = wait_check_timeout(timeout_ms, start);
                }
                request_lock.lock();
            } else {
                if (timeout_ms < 0) {
                    request.cv.wait(request_lock);
                } else {
                    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::high_resolution_clock::now() - start)
                                          .count();
                    if (elapsed_ms >= timeout_ms) {
                        is_timeout = true;
                    } else {
                        request.cv.wait_for(request_lock, std::chrono::milliseconds(timeout_ms - elapsed_ms));
                    }
                }
            }
            is_timeout = is_timeout || wait_check_timeout(timeout_ms, start);
        }
    }

    // Return timeout only if is not completed and timeout
    if (is_timeout && request.wait_context.load()) {
        request.error = -LFI_TIMEOUT;
        debug_info("[LFI] End wait with timeout " << request);
        return -LFI_TIMEOUT;
    }

    debug_info("[LFI] End wait " << request);
    return LFI_SUCCESS;
}

int LFI::test_num(lfi_request **requests, int n_requests, int how_many) {
    // If only one redirect to test
    if (how_many == 1 && n_requests == 1) {
        return test(*requests[0]);
    }
    bool need_progress_in_shm = false;
    bool need_progress_in_peer = false;
    int wait_count = how_many;
    auto loop_requests = [&] {
        for (int i = 0; i < n_requests; i++) {
            const int remaining = n_requests - i;
            // Check if impossible to reach wait count and we already found all endpoints to progress
            if (wait_count > remaining && need_progress_in_shm && need_progress_in_peer) return;

            auto &request = *requests[i];

            // Only check if it is possible to reach wait_count
            if (wait_count <= remaining) {
                std::unique_lock lock(request.mutex);
                debug_info(request);
                if (request.is_completed()) {
                    debug_info("Request already completed");
                    wait_count--;
                    if (wait_count <= 0) return;
                    continue;
                }
            }

            if (request.m_endpoint == shm_ep) {
                need_progress_in_shm = true;
            } else if (request.m_endpoint == peer_ep) {
                need_progress_in_peer = true;
            }
        }
    };
    loop_requests();

    if (wait_count == 0) {
        // Return all completed
        return 0;
    } else {
        ProgressGuard shm_guard(shm_ep, nullptr, need_progress_in_shm);
        ProgressGuard peer_guard(peer_ep, nullptr, need_progress_in_peer);
        if (shm_guard.is_leader()) {
            shm_ep.progress(true);
        }
        if (peer_guard.is_leader()) {
            peer_ep.progress(true);
        }
        loop_requests();

        if (wait_count == 0) {
            // Return all completed
            return 0;
        } else {
            return -LFI_TIMEOUT;
        }
    }
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
    bool need_progress_in_shm = false;
    bool need_progress_in_peer = false;
    if (timeout_ms >= 0) {
        start = std::chrono::high_resolution_clock::now();
    }
    // Set up the wait
    for (int i = 0; i < n_requests; i++) {
        auto &request = *requests[i];
        std::scoped_lock req_and_shared_wait_lock(request.mutex, shared_wait.wait_mutex);
        request.shared_wait_struct = &shared_wait;
        debug_info(request);
        debug_info("Request comm " << request.m_comm_id);
        if (request.is_completed()) {
            debug_info("Request already completed");
            shared_wait.wait_count--;
        } else {
            if (request.error == -LFI_TIMEOUT) {
                request.error = LFI_SUCCESS;
            }
        }
        if (request.m_endpoint == shm_ep) {
            need_progress_in_shm = true;
        } else if (request.m_endpoint == peer_ep) {
            need_progress_in_peer = true;
        }
    }

    if (shared_wait.wait_count.load() > 0) {
        ProgressGuard shm_guard(shm_ep, &shared_wait.wait_cv, need_progress_in_shm);
        ProgressGuard peer_guard(peer_ep, &shared_wait.wait_cv, need_progress_in_peer);

        while (shared_wait.wait_count.load() > 0 && !is_timeout) {
            shm_guard.try_acquire();
            peer_guard.try_acquire();

            if (shm_guard.is_leader() || peer_guard.is_leader()) {
                if (shm_guard.is_leader()) {
                    shm_ep.progress(true);
                }
                if (peer_guard.is_leader()) {
                    peer_ep.progress(true);
                }
            } else {
                if (timeout_ms < 0) {
                    std::unique_lock wait_lock(shared_wait.wait_mutex);
                    if (shared_wait.wait_count.load() > 0) {
                        shared_wait.wait_cv.wait(wait_lock);
                    }
                } else {
                    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::high_resolution_clock::now() - start)
                                          .count();
                    if (elapsed_ms >= timeout_ms) {
                        is_timeout = true;
                    } else {
                        std::unique_lock wait_lock(shared_wait.wait_mutex);
                        if (shared_wait.wait_count.load() > 0) {
                            shared_wait.wait_cv.wait_for(wait_lock, std::chrono::milliseconds(timeout_ms - elapsed_ms));
                        }
                    }
                }
            }
            is_timeout = is_timeout || wait_check_timeout(timeout_ms, start);
        }
    }

    int out_index = -1;
    // Linear counter because the func need to change the return request each time
    static std::atomic<uint32_t> linear_counter = 0;
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