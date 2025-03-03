
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
    ft_thread = std::thread(ft_thread_loop);
    ft_thread_pp = std::thread(ft_thread_ping_pong);
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
    }
    ft_cv.notify_one();
    ft_thread_pp.join();
    ft_thread.join();

    debug_info("[LFI] End");
    return LFI_SUCCESS;
}

int LFI::ft_thread_ping_pong() {
    int ret = 0;
    LFI &lfi = LFI::get_instance();
    debug_info("[LFI] Start");
    int buffer = 0;

    auto shm_comm = lfi.get_comm(LFI_ANY_COMM_SHM);
    if (!shm_comm) {
        print("Error get_comm ANY_COMM_SHM");
        return -1;
    }
    lfi_request shm_request(shm_comm);

    auto peer_comm = lfi.get_comm(LFI_ANY_COMM_PEER);
    if (!peer_comm) {
        print("Error get_comm ANY_COMM_SHM");
        return -1;
    }
    lfi_request peer_request(peer_comm);
    auto any_recv = [&](lfi_request &req) {
        int ret = lfi.async_recv(&buffer, sizeof(buffer), LFI_TAG_FT_PING, req);
        if (ret < 0) {
            print("Error in async_recv");
        }
        return ret;
    };

    ret = any_recv(shm_request);
    if (ret < 0) {
        print("Error in async_recv for the shm") return ret;
    }
    ret = any_recv(peer_request);
    if (ret < 0) {
        print("Error in async_recv for the peer") return ret;
    }

    std::vector<std::reference_wrapper<lfi_request>> requests = {shm_request, peer_request};
    while (lfi.ft_is_running) {
        int completed = lfi.wait_num(requests, 1, 10);
        int source = -1;
        if (completed == 0) {
            // SHM
            source = shm_request.source;
            // Reuse the request
            if (any_recv(shm_request) < 0) {
                print_error("peer any_recv");
            }
        } else if (completed == 1) {
            // PEER
            source = peer_request.source;
            // Reuse the request
            if (any_recv(peer_request) < 0) {
                print_error("peer any_recv");
            }
        } else if (completed == -LFI_TIMEOUT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(env::get_instance().LFI_fault_tolerance_time*1000/2));
            continue;
        } else {
            print_error("lfi wait_num");
            continue;
        }

        auto msg = lfi.send(source, &buffer, sizeof(buffer), LFI_TAG_FT_PONG);
        if (msg.error < 0) {
            // Not necesary check error
            // print("Error lfi_send pong : "<<lfi_strerror(msg.error));
            continue;
        }
    }
    debug_info("[LFI] End");
    return 0;
}

int LFI::ft_thread_loop() {
    int ret = 0;
    LFI &lfi = LFI::get_instance();
    int ms_to_wait = env::get_instance().LFI_fault_tolerance_time * 1000;
    std::unique_lock ft_lock(lfi.ft_mutex);
    std::vector<std::shared_ptr<lfi_comm>> comms_with_err;
    comms_with_err.reserve(100);
    std::unordered_map<int, lfi_request> requests;
    std::vector<std::reference_wrapper<lfi_request>> wait_requests;
    int index = 0;
    debug_info("[LFI] Start");
    auto start_loop = std::chrono::high_resolution_clock::now();

    while (lfi.ft_is_running) {
        int32_t elapsed_ms_loop = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::high_resolution_clock::now() - start_loop)
                                      .count();
        start_loop = std::chrono::high_resolution_clock::now();
        ms_to_wait = std::max(0, env::get_instance().LFI_fault_tolerance_time * 1000 - elapsed_ms_loop);
        if (lfi.ft_cv.wait_for(ft_lock, std::chrono::milliseconds(ms_to_wait), [&lfi] { return !lfi.ft_is_running; })) {
            break;
        }
        // Start the requests
        int ack = 0;
        index = 0;
        start_loop = std::chrono::high_resolution_clock::now();
        {
            std::unique_lock comms_lock(lfi.m_comms_mutex);
            requests.reserve(lfi.m_comms.size() * 2);
            wait_requests.reserve(lfi.m_comms.size() * 2);
            for (auto &[id, comm] : lfi.m_comms) {
                if (comm->rank_peer == ANY_COMM_SHM || comm->rank_peer == ANY_COMM_PEER) continue;
                if (comm->is_canceled) continue;
                int timeout_ms = std::max(0, env::get_instance().LFI_fault_tolerance_time * 1000);
                // Check if heartbeat is necesary
                if (std::chrono::duration_cast<std::chrono::milliseconds>(start_loop - comm->get_request_time()).count() < timeout_ms) continue;
                {
                    auto [it, _] = requests.emplace(index++, comm);
                    auto &send_request = it->second;
                    debug_info("[LFI] Send ft ack comm " << id << " " << std::hex << &send_request << std::dec);
                    ret = lfi.async_send(&ack, sizeof(ack), LFI_TAG_FT_PING, send_request, timeout_ms);
                    if (ret < 0) {
                        comm->ft_error = true;
                        comms_with_err.push_back(comm);
                        debug_info("[LFI] Error in Send ft ack comm " << id << " " << std::hex << &send_request
                                                                      << std::dec);
                        continue;
                    }
                    wait_requests.emplace_back(send_request);
                }
                {
                    auto [it, _] = requests.emplace(index++, comm);
                    auto &recv_request = it->second;
                    debug_info("[LFI] Recv ft ack comm " << id << " " << std::hex << &recv_request << std::dec);
                    ret = lfi.async_recv(&ack, sizeof(ack), LFI_TAG_FT_PONG, recv_request, timeout_ms);
                    if (ret < 0) {
                        comm->ft_error = true;
                        comms_with_err.push_back(comm);
                        debug_info("[LFI] Error in Recv ft ack comm " << id << " " << std::hex << &recv_request
                                                                      << std::dec);
                        continue;
                    }
                    wait_requests.emplace_back(recv_request);
                }
            }
        }
        // TODO: check if is necesary to be in comms_lock
        // If there are no comms continue
        if (comms_with_err.size() == 0 && requests.size() == 0 && wait_requests.size() == 0) {
            continue;
        }
        
        debug_info("[LFI] FT do "<<requests.size()/2<<" heartbeats");

        lfi.wait_num(wait_requests, wait_requests.size(), env::get_instance().LFI_fault_tolerance_time * 1000);

        for (auto &request_ref : wait_requests) {
            auto &request = request_ref.get();
            if (request.error < 0) {
                debug_info("[LFI] cancel request in comm with error " << request.m_comm->rank_peer);
                lfi.cancel(request);
                comms_with_err.push_back(request.m_comm);
            }
        }

        for (auto &comm : comms_with_err) {
            std::unique_lock lock(comm->ft_mutex);
            debug_info("[LFI] cancel all request in comm with error " << comm->rank_peer);
            for (auto &request : comm->ft_requests) {
                if (request == nullptr) continue;
                debug_info("[LFI] cancel " << request->to_string());
                lfi.cancel(*request);
                debug_info("[LFI] canceled " << request->to_string());
            }
            comm->ft_requests.clear();

            debug_info("[LFI] close comm with error " << comm->rank_peer);
            comm->is_canceled = true;
        }
        comms_with_err.clear();

        wait_requests.clear();
        requests.clear();
    }

    debug_info("[LFI] End");
    return ret;
}
}  // namespace LFI