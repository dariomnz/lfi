
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

namespace LFI {

int LFI::ft_thread_start() {
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
    if (!env::get_instance().LFI_fault_tolerance) return LFI_SUCCESS;

    debug_info("[LFI] Start");

    {
        std::unique_lock lock(ft_mutex);
        if (!ft_is_running) return LFI_SUCCESS;
        ft_is_running = false;
        ft_cv.notify_one();
    }
    ft_thread_pp.join();

    debug_info("[LFI] End");
    return LFI_SUCCESS;
}

int LFI::ft_thread_ping_pong() {
    LFI &lfi = LFI::get_instance();
    debug_info("[LFI] Start");

    std::unique_lock ft_lock(lfi.ft_mutex);
    while (lfi.ft_is_running) {
        if (!lfi.shm_ep.progress.load()) {
            // debug_info("[LFI] running ft ping pong thread shm");
            lfi.progress(lfi.shm_ep);
        }
        if (!lfi.peer_ep.progress.load()) {
            // debug_info("[LFI] running ft ping pong thread peer");
            lfi.progress(lfi.peer_ep);
        }
        lfi.ft_cv.wait_for(ft_lock, std::chrono::milliseconds(100));
    }
    debug_info("[LFI] End");
    return 0;
}

int LFI::ft_setup_ping_pong() {
    static int dummy = 0;

    auto create_ping = [this](auto any_comm) {
        auto shm_comm = get_comm(any_comm);
        if (!shm_comm) {
            print("Error get_comm ANY_COMM_SHM " << any_comm);
            return -1;
        }
        auto ft_ping_shm = std::make_shared<lfi_request>(shm_comm);
        ft_ping_shm->callback = [this, ft_ping_shm](int error) mutable {
            if (error == LFI_SUCCESS) {
                debug_info("[LFI] send pong to " << ft_ping_shm->source);
                auto comm = get_comm(ft_ping_shm->source);
                if (!comm) {
                    print("Error get_comm of " << ft_ping_shm->source);
                    return -1;
                }
                auto fi_pong = std::make_shared<lfi_request>(comm);
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

    const size_t ping_pong_count = 4;
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

int LFI::ft_one_loop(lfi_ep &lfi_ep) {
    static int dummy = 0;
    static std::mutex m;
    std::unique_lock unique_m(m, std::defer_lock);
    if (!unique_m.try_lock()) return LFI_SUCCESS;
    std::unique_lock lock(lfi_ep.requests_mutex);
    static std::vector<std::shared_ptr<lfi_comm>> canceled_coms;
    canceled_coms.reserve(10);
    const uint32_t retrys = 100;
    int count_sended = 0;
    int32_t ft_ms = std::max(100, env::get_instance().LFI_fault_tolerance_time * 1000);
#ifdef DEBUG
    if (lfi_ep.ft_comms.size() > 0) {
        debug_info("Comms to ft " << lfi_ep.ft_comms.size());
    }
#endif
    for (auto &&comm : lfi_ep.ft_comms) {
        // Check if necesary emit ping pong
        auto now = std::chrono::high_resolution_clock::now();
        if (comm->ft_current_status == lfi_comm::ft_status::IDLE) {
            int32_t elapsed_ms_req =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - comm->last_request_time).count();
            if (elapsed_ms_req > ft_ms) {
                debug_info("[LFI] comm " << comm->rank_peer << " elapsed_ms_req(" << elapsed_ms_req << ") > ft_ms("
                                         << ft_ms << ")");
                debug_info("[LFI] comm " << comm->rank_peer << " IDLE -> SEND_PING");
                comm->ft_current_status = lfi_comm::ft_status::SEND_PING;
                comm->ft_ping_retry = 0;
                comm->ft_pong_retry = 0;
            }
        }
        if (comm->ft_current_status == lfi_comm::ft_status::SEND_PING) {
            debug_info("[LFI] comm " << comm->rank_peer << " SEND_PING");
            if (!comm->ft_ping) {
                comm->ft_ping = std::make_unique<lfi_request>(comm);
            } else {
                comm->ft_ping->reset();
            }
            debug_info("[LFI] comm " << comm->rank_peer << " async_send PING try " << comm->ft_ping_retry);
            comm->ft_ping_retry++;
            auto ret_ping = async_send(&dummy, 0, LFI_TAG_FT_PING, *comm->ft_ping, 0);
            if (ret_ping == -LFI_TIMEOUT && comm->ft_ping_retry < retrys) {
                debug_info("[LFI] async_send PING " << comm->rank_peer << " timeout");
                continue;
            }
            if ((ret_ping < 0 && ret_ping != -LFI_TIMEOUT) ||
                (ret_ping == -LFI_TIMEOUT && comm->ft_ping_retry >= retrys)) {
                canceled_coms.emplace_back(comm);
                debug_info("[LFI] comm " << comm->rank_peer << " SEND_PING -> ERROR");
                comm->ft_current_status = lfi_comm::ft_status::ERROR;
                continue;
            }

            debug_info("[LFI] comm " << comm->rank_peer << " SEND_PING -> RECV_PONG");
            comm->ft_current_status = lfi_comm::ft_status::RECV_PONG;
        }

        if (comm->ft_current_status == lfi_comm::ft_status::RECV_PONG) {
            debug_info("[LFI] comm " << comm->rank_peer << " RECV_PONG");
            if (!comm->ft_pong) {
                comm->ft_pong = std::make_unique<lfi_request>(comm);
            } else {
                comm->ft_pong->reset();
            }
            debug_info("[LFI] comm " << comm->rank_peer << " async_recv PONG try " << comm->ft_pong_retry);
            comm->ft_pong_retry++;
            auto ret_pong = async_recv(&dummy, 0, LFI_TAG_FT_PONG, *comm->ft_pong, 0);
            if (ret_pong == -LFI_TIMEOUT && comm->ft_pong_retry < retrys) {
                debug_info("[LFI] async_recv PONG " << comm->rank_peer << " timeout");
                continue;
            }
            if ((ret_pong < 0 && ret_pong != -LFI_TIMEOUT) ||
                (ret_pong == -LFI_TIMEOUT && comm->ft_pong_retry >= retrys)) {
                canceled_coms.emplace_back(comm);
                debug_info("[LFI] comm " << comm->rank_peer << " RECV_PONG -> ERROR");
                comm->ft_current_status = lfi_comm::ft_status::ERROR;
                continue;
            }
            comm->ft_pp_time = std::chrono::high_resolution_clock::now();
            debug_info("[LFI] comm " << comm->rank_peer << " RECV_PONG -> WAIT_PING_PONG");
            comm->ft_current_status = lfi_comm::ft_status::WAIT_PING_PONG;
            count_sended++;
        }
        if (comm->ft_current_status == lfi_comm::ft_status::WAIT_PING_PONG) {
            if (comm->ft_ping->is_completed() && comm->ft_pong->is_completed()) {
                debug_info("[LFI] comm " << comm->rank_peer << " WAIT_PING_PONG -> IDLE");
                comm->ft_current_status = lfi_comm::ft_status::IDLE;
            } else {
                int32_t elapsed_ms_pp =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - comm->ft_pp_time).count();
                if (elapsed_ms_pp > ft_ms) {
                    debug_info("[LFI] comm " << comm->rank_peer << " elapsed_ms_pp(" << elapsed_ms_pp << ") > ft_ms("
                                             << ft_ms << ")");
                    canceled_coms.emplace_back(comm);
                    debug_info("[LFI] comm " << comm->rank_peer << " WAIT_PING_PONG -> ERROR");
                    comm->ft_current_status = lfi_comm::ft_status::ERROR;
                }
            }
        }
    }

    for (auto &&comm : canceled_coms) {
        debug_info("Remove " << comm->rank_peer << " comm from ft_comms");
        ft_cancel_comm(*comm);
        lfi_ep.ft_comms.erase(comm);
    }

    canceled_coms.clear();
    if (count_sended > 0) {
        debug_info("Sended " << count_sended << " pings");
    }
    return 0;
}

int LFI::ft_cancel_comm(lfi_comm &comm) {
    std::unique_lock lock(comm.ft_mutex);
    debug_info("[LFI] mark canceled comm " << comm.rank_peer);
    comm.is_canceled = true;

    debug_info("[LFI] cancel all request in comm with error " << comm.rank_peer);
    for (auto &request : comm.ft_requests) {
        if (request == nullptr) continue;
        debug_info("[LFI] cancel " << request->to_string());
        cancel(*request);
        debug_info("[LFI] canceled " << request->to_string());
    }
    comm.ft_requests.clear();

    return 0;
}
}  // namespace LFI