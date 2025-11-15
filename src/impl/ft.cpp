
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

// #define DEBUG
#include <chrono>
#include <mutex>

#include "impl/debug.hpp"
#include "impl/profiler.hpp"
#include "impl/env.hpp"
#include "impl/lfi.hpp"
#include "lfi.h"
#include "lfi_error.h"

namespace LFI {

int LFI::ft_thread_start() {
    LFI_PROFILE_FUNCTION();
    if (!env::get_instance().LFI_fault_tolerance) return LFI_SUCCESS;

    debug_info("[LFI] Start");
    {
        std::unique_lock lock(ft_mutex);
        if (ft_is_running) return LFI_SUCCESS;
        ft_is_running = true;
    }
    ft_thread_pp = std::thread(ft_thread_ping_pong);
    ft_setup_ping_pong();
    debug_info("[LFI] End");
    return LFI_SUCCESS;
}

int LFI::ft_thread_destroy() {
    LFI_PROFILE_FUNCTION();
    if (!env::get_instance().LFI_fault_tolerance) return LFI_SUCCESS;

    debug_info("[LFI] Start");

    {
        std::unique_lock lock(ft_mutex);
        if (!ft_is_running) return LFI_SUCCESS;
        ft_is_running = false;
        ft_cv.notify_one();
    }
    if (ft_thread_pp.joinable()) {
        ft_thread_pp.join();
    }

    debug_info("[LFI] End");
    return LFI_SUCCESS;
}

int LFI::ft_thread_ping_pong() {
    LFI_PROFILE_FUNCTION();
    LFI &lfi = LFI::get_instance();
    debug_info("[LFI] Start");

    auto last_debug_dump = std::chrono::high_resolution_clock::now();

    std::unique_lock ft_lock(lfi.ft_mutex);
    while (lfi.ft_is_running) {
        if (env::get_instance().LFI_debug_dump_interval > 0) {
            auto now = std::chrono::high_resolution_clock::now();
            auto ellapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_debug_dump).count();
            if (ellapsed > (env::get_instance().LFI_debug_dump_interval * 1000)) {
                last_debug_dump = now;
                lfi.dump_stats();
            }
        }

        int progresed_shm = 0;
        int progresed_peer = 0;
        if (!lfi.shm_ep.in_progress.load()) {
            // debug_info("[LFI] running ft ping pong thread shm");
            progresed_shm = lfi.shm_ep.progress();
        }
        if (!lfi.peer_ep.in_progress.load()) {
            // debug_info("[LFI] running ft ping pong thread peer");
            progresed_peer = lfi.peer_ep.progress();
        }
        if (progresed_shm > 0 || progresed_peer > 0) continue;
        lfi.ft_cv.wait_for(ft_lock, std::chrono::milliseconds(1));
    }
    debug_info("[LFI] End");
    return 0;
}

int LFI::ft_setup_ping_pong() {
    LFI_PROFILE_FUNCTION();
    static int dummy = 0;

    auto create_ping = [this](auto any_comm) {
        auto comm = get_comm(any_comm);
        if (!comm) {
            print("Error get_comm ANY_COMM_SHM " << any_comm);
            return -1;
        }
        auto ft_ping_shm = std::make_shared<lfi_request>(*comm);
        ft_ping_shm->callback = [this, ft_ping_shm](int error) mutable {
            if (error == LFI_SUCCESS) {
                debug_info("[LFI] send pong to " << ft_ping_shm->source);
                auto comm = get_comm(ft_ping_shm->source);
                if (!comm) {
                    print("Error get_comm of " << ft_ping_shm->source);
                    return -1;
                }
                auto fi_pong = std::make_shared<lfi_request>(*comm);
                fi_pong->callback = [fi_pong](int error) mutable {
                    (void)error;
                    fi_pong.reset();
                };
                int ret = async_send(&dummy, 0, LFI_TAG_FT_PONG, *fi_pong);
                if (ret < 0) {
                    print("Error in async_send");
                    return ret;
                }
            }

            // Repost recv PING
            int ret = async_recv(&dummy, 0, LFI_TAG_FT_PING, *ft_ping_shm);
            if (ret < 0) {
                print("Error in async_recv");
                return ret;
            }
            return 0;
        };
        int ret = async_recv(&dummy, 0, LFI_TAG_FT_PING, *ft_ping_shm);
        if (ret < 0) {
            print("Error in async_recv");
            return ret;
        }
        return 0;
    };

    const size_t ping_pong_count = 8;
    for (size_t i = 0; i < ping_pong_count; i++) {
        debug_info("[LFI] create ft_ping_shm LFI_ANY_COMM_SHM");
        if (create_ping(LFI_ANY_COMM_SHM) < 0) {
            print("Error create_ping LFI_ANY_COMM_SHM");
        }
        debug_info("[LFI] create ft_ping_shm LFI_ANY_COMM_PEER");
        if (create_ping(LFI_ANY_COMM_PEER) < 0) {
            print("Error create_ping LFI_ANY_COMM_PEER");
        }
    }

    return 0;
}

int LFI::ft_one_loop(lfi_endpoint &lfi_ep) {
    LFI_PROFILE_FUNCTION();
    static int dummy = 0;
    static std::mutex m;
    std::unique_lock unique_m(m, std::defer_lock);
    if (!unique_m.try_lock()) return LFI_SUCCESS;

    static std::vector<uint32_t> canceled_coms;
    canceled_coms.reserve(10);
    int count_sended = 0;
    int32_t ft_ms = std::max(100, env::get_instance().LFI_fault_tolerance_time * 1000);

    auto innerloop = [&](lfi_comm *comm) {
        // Check if necesary emit ping pong
        auto now = std::chrono::high_resolution_clock::now();
        if (comm->ft_current_status == lfi_comm::ft_status::IDLE) {
            int32_t elapsed_ms_req =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - comm->get_request_time()).count();
            if (elapsed_ms_req > ft_ms) {
                debug_info("[LFI] comm " << comm->rank_peer << " elapsed_ms_req(" << elapsed_ms_req << ") > ft_ms("
                                         << ft_ms << ")");
                debug_info("[LFI] comm " << comm->rank_peer << " IDLE -> SEND_PING");
                comm->ft_current_status = lfi_comm::ft_status::SEND_PING;
                comm->ft_ping_time_point = std::chrono::high_resolution_clock::now();
            }
        }
        if (comm->ft_current_status == lfi_comm::ft_status::SEND_PING) {
            debug_info("[LFI] comm " << comm->rank_peer << " SEND_PING");
            if (!comm->ft_ping) {
                comm->ft_ping = std::make_unique<lfi_request>(*comm);
            } else {
                comm->ft_ping->reset();
            }
            int32_t ping_ellapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - comm->ft_ping_time_point).count();
            debug_info("[LFI] comm " << comm->rank_peer << " async_send PING try " << ping_ellapsed);
            auto ret_ping = async_send(&dummy, 0, LFI_TAG_FT_PING, *comm->ft_ping, 0);

            if (ret_ping == -LFI_TIMEOUT && ping_ellapsed < ft_ms) {
                debug_info("[LFI] async_send PING " << comm->rank_peer << " timeout");
                return;
            }
            if ((ret_ping < 0 && ret_ping != -LFI_TIMEOUT) || (ret_ping == -LFI_TIMEOUT && ping_ellapsed >= ft_ms)) {
                canceled_coms.emplace_back(comm->rank_peer);
                debug_info("[LFI] comm " << comm->rank_peer << " SEND_PING -> ERROR " << lfi_strerror(ret_ping));
                comm->ft_current_status = lfi_comm::ft_status::ERROR;
                return;
            }

            debug_info("[LFI] comm " << comm->rank_peer << " SEND_PING -> RECV_PONG");
            comm->ft_current_status = lfi_comm::ft_status::RECV_PONG;
            comm->ft_pong_time_point = std::chrono::high_resolution_clock::now();
        }

        if (comm->ft_current_status == lfi_comm::ft_status::RECV_PONG) {
            debug_info("[LFI] comm " << comm->rank_peer << " RECV_PONG");
            if (!comm->ft_pong) {
                comm->ft_pong = std::make_unique<lfi_request>(*comm);
            } else {
                comm->ft_pong->reset();
            }
            int32_t pong_ellapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - comm->ft_pong_time_point).count();
            debug_info("[LFI] comm " << comm->rank_peer << " async_recv PONG try " << pong_ellapsed);
            auto ret_pong = async_recv(&dummy, 0, LFI_TAG_FT_PONG, *comm->ft_pong, 0);
            if (ret_pong == -LFI_TIMEOUT && pong_ellapsed < ft_ms) {
                debug_info("[LFI] async_recv PONG " << comm->rank_peer << " timeout");
                return;
            }
            if ((ret_pong < 0 && ret_pong != -LFI_TIMEOUT) || (ret_pong == -LFI_TIMEOUT && pong_ellapsed >= ft_ms)) {
                canceled_coms.emplace_back(comm->rank_peer);
                debug_info("[LFI] comm " << comm->rank_peer << " RECV_PONG -> ERROR " << lfi_strerror(ret_pong));
                comm->ft_current_status = lfi_comm::ft_status::ERROR;
                return;
            }
            debug_info("[LFI] comm " << comm->rank_peer << " RECV_PONG -> WAIT_PING_PONG");
            comm->ft_current_status = lfi_comm::ft_status::WAIT_PING_PONG;
            comm->ft_pong_time_point = std::chrono::high_resolution_clock::now();
            count_sended++;
        }
        if (comm->ft_current_status == lfi_comm::ft_status::WAIT_PING_PONG) {
            if (comm->ft_ping->is_completed() && comm->ft_pong->is_completed()) {
                debug_info("[LFI] comm " << comm->rank_peer << " WAIT_PING_PONG -> IDLE");
                comm->ft_current_status = lfi_comm::ft_status::IDLE;
            } else {
                int32_t elapsed_ms_pp =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - comm->ft_pong_time_point).count();
                if (elapsed_ms_pp > ft_ms) {
                    debug_info("[LFI] comm " << comm->rank_peer << " elapsed_ms_pp(" << elapsed_ms_pp << ") > ft_ms("
                                             << ft_ms << ")");
                    canceled_coms.emplace_back(comm->rank_peer);
                    debug_info("[LFI] comm " << comm->rank_peer << " WAIT_PING_PONG -> ERROR");
                    comm->ft_current_status = lfi_comm::ft_status::ERROR;
                }
            }
        }
    };

    // Report errors in comms in the any_comm requests
    int64_t ft_any_comm_requests_size;
    {
        std::unique_lock lock(lfi_ep.ft_any_comm_requests_mutex);
        ft_any_comm_requests_size = lfi_ep.ft_any_comm_requests.size();
    }
    if (ft_any_comm_requests_size > 0) {
        std::vector<uint32_t> temp_comms_ids;
        {
            std::unique_lock lock(m_comms_mutex);
            temp_comms_ids.reserve(m_comms.size());
            for (auto &&comm : m_comms) {
                temp_comms_ids.emplace_back(comm.first);
            }
        }
        for (auto &&comm_id : temp_comms_ids) {
            lfi_comm *comm = nullptr;
            {
                std::unique_lock lock(m_comms_mutex);
                auto it = m_comms.find(comm_id);
                if (it == m_comms.end()) continue;
                comm = it->second.get();
            }
            if (!comm) continue;
            if (comm && comm->m_ep == lfi_ep && comm->rank_peer != LFI_ANY_COMM_SHM &&
                comm->rank_peer != LFI_ANY_COMM_PEER) {
                // debug_info("Check ft in comm " << comm->rank_peer << " from all comms");
                innerloop(comm);
            }
        }
    } else {
        lfi_ep.ft_comms_mutex.lock();
        std::unordered_set<lfi_comm *> temp_ft_comms(lfi_ep.ft_comms);
        lfi_ep.ft_comms_mutex.unlock();
        for (auto &&comm : temp_ft_comms) {
            // debug_info("Check ft in comm " << comm->rank_peer << " from lfi_ep.ft_comms");
            innerloop(comm);
        }
    }
    {
        for (auto &&comm_id : canceled_coms) {
            debug_info("Remove " << comm_id << " comm from ft_comms");
            auto comm_ptr = get_comm(comm_id);
            if (!comm_ptr) continue;
            ft_cancel_comm(*comm_ptr);
        }
    }

    {
        std::unique_lock lock(lfi_ep.ft_any_comm_requests_mutex);
        ft_any_comm_requests_size = lfi_ep.ft_any_comm_requests.size();
    }
    std::unique_lock ft_pending_failed_lock(lfi_ep.ft_pending_failed_comms_mutex);
    if (ft_any_comm_requests_size > 0 && (lfi_ep.ft_pending_failed_comms.size() > 0 || canceled_coms.size() > 0)) {
        {
            // First if there are pending use them cancelling any comm requests
            std::unordered_set<lfi_request *> temp_any_comm_requests;
            {
                std::unique_lock lock(lfi_ep.ft_any_comm_requests_mutex);
                temp_any_comm_requests = lfi_ep.ft_any_comm_requests;
            }
            auto size = std::min(lfi_ep.ft_pending_failed_comms.size(), temp_any_comm_requests.size());
            auto any_req_it = temp_any_comm_requests.begin();
            auto canceled_comm_it = lfi_ep.ft_pending_failed_comms.begin();
            for (uint32_t i = 0; i < size; i++) {
                auto &any_req = *any_req_it;
                auto &canceled_comm = *canceled_comm_it;

                debug_info("Use pending error to report to any_comm request " << canceled_comm);
                {
                    std::unique_lock req_lock(any_req->mutex);
                    any_req->source = canceled_comm;
                    any_req->error = -LFI_BROKEN_COMM;
                }
                any_req->cancel();

                // Next iterators
                ++any_req_it;
                canceled_comm_it = lfi_ep.ft_pending_failed_comms.erase(canceled_comm_it);
            }
        }
        ft_pending_failed_lock.unlock();

        {
            // With the remaining any comm requests use the current cancelled
            std::unordered_set<lfi_request *> temp_any_comm_requests;
            {
                std::unique_lock lock(lfi_ep.ft_any_comm_requests_mutex);
                temp_any_comm_requests = lfi_ep.ft_any_comm_requests;
            }
            auto size = std::min(canceled_coms.size(), temp_any_comm_requests.size());
            auto any_req_it = temp_any_comm_requests.begin();
            auto cancel_comm_it = canceled_coms.begin();
            for (uint32_t i = 0; i < size; i++) {
                auto &any_req = *any_req_it;
                auto &cancel_comm = *cancel_comm_it;

                debug_info("Use th cancelled comm to report to any_comm request " << cancel_comm);
                {
                    std::unique_lock req_lock(any_req->mutex);
                    any_req->source = cancel_comm;
                    any_req->error = -LFI_BROKEN_COMM;
                }
                any_req->cancel();

                // Next iterators
                ++any_req_it;
                ++cancel_comm_it;
            }

            // Save the others
            while (cancel_comm_it != canceled_coms.end()) {
                auto &cancel_comm = *cancel_comm_it;
                debug_info("Save to pending request canceled comms that coudnt be reported to any comms "
                           << cancel_comm);
                {
                    std::unique_lock lock(lfi_ep.ft_pending_failed_comms_mutex);
                    lfi_ep.ft_pending_failed_comms.emplace(cancel_comm);
                }

                // Next iterators
                ++cancel_comm_it;
            }
        }
    }

    canceled_coms.clear();
    if (count_sended > 0) {
        debug_info("Sended " << count_sended << " pings");
    }
    return 0;
}

int LFI::ft_cancel_comm(lfi_comm &comm) {
    LFI_PROFILE_FUNCTION();
    std::unique_lock lock(comm.ft_mutex);
    debug_info("[LFI] mark canceled comm " << comm.rank_peer);
    comm.is_canceled = true;

    debug_info("[LFI] cancel all request in comm with error " << comm.rank_peer);
    std::unordered_set<lfi_request *> temp_requests(comm.ft_requests);
    lock.unlock();
    for (auto &request : temp_requests) {
        if (request == nullptr) continue;
        request->cancel();
    }
    {
        std::unique_lock ft_lock(comm.m_ep.ft_comms_mutex);
        comm.m_ep.ft_comms.erase(&comm);
    }
    lock.lock();
    comm.ft_requests.clear();

    comm.ft_comm_count = 0;

    return 0;
}
}  // namespace LFI