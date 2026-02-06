
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

#include "helpers.hpp"
#include "impl/debug.hpp"
#include "impl/env.hpp"
#include "impl/lfi.hpp"
#include "impl/profiler.hpp"
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
        if (env::get_instance().LFI_use_shm) {
            ProgressGuard shm_progress(lfi.shm_ep);
            if (shm_progress.is_leader()) {
                // debug_info("[LFI] running ft ping pong thread shm");
                progresed_shm = lfi.shm_ep.progress(true);
            }
        }
        {
            ProgressGuard peer_progress(lfi.peer_ep);
            if (peer_progress.is_leader()) {
                // debug_info("[LFI] running ft ping pong thread peer");
                progresed_peer = lfi.peer_ep.progress(true);
            }
        }
        // If there are some progress rerun without sleep
        if (progresed_shm > 0 || progresed_peer > 0) continue;

        lfi.ft_cv.wait_for(ft_lock, std::chrono::milliseconds(1));
    }
    debug_info("[LFI] End");
    return 0;
}

void pong_callback([[maybe_unused]] int error, void *context) {
    lfi_request *req = static_cast<lfi_request *>(context);
    delete req;
}

void ping_callback([[maybe_unused]] int error, void *context) {
    static int dummy = 0;
    LFI &lfi = LFI::get_instance();
    lfi_request *req = static_cast<lfi_request *>(context);
    if (error == LFI_SUCCESS) {
        debug_info("[LFI] send pong to " << req->source);
        auto [lock, comm] = lfi.get_comm_and_mutex(req->source);
        if (!comm) {
            print("Error get_comm of " << req->source);
            return;
        }
        auto fi_pong = new lfi_request(comm->m_endpoint, comm->rank_peer);
        fi_pong->callback = pong_callback;
        fi_pong->callback_ctx = fi_pong;
        int ret = lfi.async_send(&dummy, 0, LFI_TAG_FT_PONG, *fi_pong, true);
        if (ret < 0) {
            print("Error in async_send " << ret << " " << lfi_strerror(ret));
            return;
        }
    }

    // Repost recv PING
    int ret = lfi.async_recv(&dummy, 0, LFI_TAG_FT_PING, *req, true);
    if (ret < 0) {
        print("Error in async_recv " << ret << " " << lfi_strerror(ret));
        return;
    }
}

int LFI::ft_setup_ping_pong() {
    LFI_PROFILE_FUNCTION();
    static int dummy = 0;

    auto create_ping = [this](auto any_comm) {
        auto [lock, comm] = get_comm_and_mutex(any_comm);
        if (!comm) {
            print("Error get_comm ANY_COMM_SHM " << any_comm);
            return -1;
        }
        auto &ft_ping = comm->m_endpoint.ft_ping_pongs.emplace_back(
            std::make_unique<lfi_request>(comm->m_endpoint, comm->rank_peer));
        ft_ping->callback = ping_callback;
        ft_ping->callback_ctx = ft_ping.get();
        int ret = async_recv(&dummy, 0, LFI_TAG_FT_PING, *ft_ping, true);
        if (ret < 0) {
            print("Error in async_recv");
            return ret;
        }
        return 0;
    };

    const size_t ping_pong_count = 8;
    shm_ep.ft_ping_pongs.reserve(ping_pong_count);
    peer_ep.ft_ping_pongs.reserve(ping_pong_count);
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
            decltype(comm->last_request_time) last_request_time;
            {
                std::unique_lock ft_lock(comm->ft_mutex);
                last_request_time = comm->last_request_time;
            }
            int32_t elapsed_ms_req =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last_request_time).count();
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
                comm->ft_ping = std::make_unique<lfi_request>(comm->m_endpoint, comm->rank_peer);
            } else {
                comm->ft_ping->reset();
            }
            debug_info("[LFI] comm " << comm->rank_peer << " async_send PING try");
            auto ret_ping = async_send(&dummy, 0, LFI_TAG_FT_PING, *comm->ft_ping, true);
            if (ret_ping < 0) {
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
                comm->ft_pong = std::make_unique<lfi_request>(comm->m_endpoint, comm->rank_peer);
            } else {
                comm->ft_pong->reset();
            }
            debug_info("[LFI] comm " << comm->rank_peer << " async_recv PONG try");
            auto ret_pong = async_recv(&dummy, 0, LFI_TAG_FT_PONG, *comm->ft_pong, true);
            if (ret_pong < 0) {
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
            std::scoped_lock ping_pong_lock(comm->ft_ping->mutex, comm->ft_pong->mutex);
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
    // int64_t ft_any_comm_requests_size;
    std::unique_lock ft_ep_lock(lfi_ep.ft_mutex, std::defer_lock);
    {
        std::shared_lock comm_lock(m_comms_mutex);
        ft_ep_lock.lock();
        if (lfi_ep.ft_any_comm_requests.size() > 0) {
            ft_ep_lock.unlock();
            for (auto &&[comm_id, comm] : m_comms) {
                if (comm && comm->m_endpoint == lfi_ep && comm->rank_peer != LFI_ANY_COMM_SHM &&
                    comm->rank_peer != LFI_ANY_COMM_PEER) {
                    // debug_info("Check ft in comm " << comm->rank_peer << " from all comms");
                    innerloop(comm.get());
                }
            }
        } else {
            std::unordered_set<lfi_comm *> temp_ft_comms(lfi_ep.ft_comms);
            ft_ep_lock.unlock();
            for (auto &&comm : temp_ft_comms) {
                // debug_info("Check ft in comm " << comm->rank_peer << " from lfi_ep.ft_comms");
                innerloop(comm);
            }
        }
        for (auto &&comm_id : canceled_coms) {
            debug_info("Remove " << comm_id << " comm from ft_comms");
            auto comm_ptr = get_comm_internal(comm_lock, comm_id);
            if (!comm_ptr) continue;
            ft_cancel_comm(*comm_ptr);
        }
    }
    ft_ep_lock.lock();

    if (lfi_ep.ft_any_comm_requests.size() > 0 &&
        (lfi_ep.ft_pending_failed_comms.size() > 0 || canceled_coms.size() > 0)) {
        static std::vector<lfi_request *> request_to_cancel;
        request_to_cancel.reserve(10);

        {
            // First if there are pending use them cancelling any comm requests
            auto size = std::min(lfi_ep.ft_pending_failed_comms.size(), lfi_ep.ft_any_comm_requests.size());
            auto any_req_it = lfi_ep.ft_any_comm_requests.begin();
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
                // Cancel without mutexs
                request_to_cancel.emplace_back(any_req);

                // Next iterators
                ++any_req_it;
                canceled_comm_it = lfi_ep.ft_pending_failed_comms.erase(canceled_comm_it);
            }
        }

        {
            // With the remaining any comm requests use the current cancelled
            auto size = std::min(canceled_coms.size(), lfi_ep.ft_any_comm_requests.size());
            auto any_req_it = lfi_ep.ft_any_comm_requests.begin();
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
                // Cancel without mutexs
                request_to_cancel.emplace_back(any_req);

                // Next iterators
                ++any_req_it;
                ++cancel_comm_it;
            }

            // Save the others
            while (cancel_comm_it != canceled_coms.end()) {
                auto &cancel_comm = *cancel_comm_it;
                debug_info("Save to pending request canceled comms that coudnt be reported to any comms "
                           << cancel_comm);
                lfi_ep.ft_pending_failed_comms.emplace(cancel_comm);

                // Next iterators
                ++cancel_comm_it;
            }
        }

        {
            ft_ep_lock.unlock();
            // Cancel request without mutexs
            for (auto req : request_to_cancel) {
                req->cancel();
            }
            request_to_cancel.clear();
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
    // In a temp set because cancel call complete that erase the request from ft_requests
    std::unordered_set<lfi_request *> temp_requests(comm.ft_requests);
    for (auto &request : temp_requests) {
        if (request == nullptr) continue;
        request->cancel();
    }
    {
        std::unique_lock ft_lock(comm.m_endpoint.ft_mutex);
        comm.m_endpoint.ft_comms.erase(&comm);
    }
    comm.ft_requests.clear();

    comm.ft_comm_count = 0;

    return 0;
}
}  // namespace LFI