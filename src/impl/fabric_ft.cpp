
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

#include "impl/fabric.hpp"
#include "impl/debug.hpp"
#include "impl/env.hpp"

namespace LFI
{

    int LFI::ft_thread_start()
    {
        LFI &lfi = LFI::get_instance();
        if (!env::get_instance().LFI_fault_tolerance)
            return 0;

        debug_info("[LFI] Start");
        {
            std::lock_guard<std::mutex> lock(lfi.ft_mutex);
            if (lfi.ft_is_running) return 0;
            lfi.ft_is_running = true;
        }
        lfi.ft_thread = std::thread(ft_thread_loop);
        debug_info("[LFI] End");
        return 0;
    }

    int LFI::ft_thread_destroy()
    {
        LFI &lfi = LFI::get_instance();
        if (!env::get_instance().LFI_fault_tolerance)
            return 0;

        debug_info("[LFI] Start");

        {
            std::lock_guard<std::mutex> lock(lfi.ft_mutex);
            if (!lfi.ft_is_running) return 0;
            lfi.ft_is_running = false;
        }
        lfi.ft_cv.notify_one();
        lfi.ft_thread.join();

        debug_info("[LFI] End");
        return 0;
    }

    int LFI::ft_thread_loop()
    {
        int ret = 0;
        LFI &lfi = LFI::get_instance();
        int ms_to_wait = env::get_instance().LFI_fault_tolerance_time * 1000;
        std::unique_lock<std::mutex> ft_lock(lfi.ft_mutex);
        std::vector<uint32_t> comms_with_err;
        comms_with_err.reserve(100);
        std::unordered_map<int, fabric_request> requests;
        int index = 0;
        debug_info("[LFI] Start");
        auto start_loop = std::chrono::high_resolution_clock::now();

        while (lfi.ft_is_running)
        {
            int32_t elapsed_ms_loop = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_loop).count(); 
            start_loop = std::chrono::high_resolution_clock::now();
            ms_to_wait = std::max(0, env::get_instance().LFI_fault_tolerance_time * 1000 - elapsed_ms_loop);
            if (lfi.ft_cv.wait_for(ft_lock, std::chrono::milliseconds(ms_to_wait), [&lfi]
                                        { return !lfi.ft_is_running; }))
            {
                break;
            }
            ft_lock.unlock();
            // Start the requests
            std::unique_lock comms_lock(lfi.m_mutex);
            int ack = 0;
            fabric_msg msg;
            index = 0;
            requests.reserve(lfi.m_comms.size()*2);
            for (auto &[id, comm] : lfi.m_comms)
            {
                if (comm.rank_peer == ANY_COMM_SHM || comm.rank_peer == ANY_COMM_PEER) continue;
                if (comm.is_canceled) continue;
                int timeout_ms = std::max(0, env::get_instance().LFI_fault_tolerance_time*1000);
                {   
                    auto [it, _] = requests.emplace(index++, comm);
                    auto& send_request = it->second;
                    debug_info("[LFI] Send ft ack comm "<<id<<" "<<std::hex<<&send_request<<std::dec);
                    msg = async_send(&ack, sizeof(ack), LFI_TAG_FT, send_request, timeout_ms);
                    if (msg.error < 0){
                        comm.ft_error = true;
                        comms_with_err.push_back(id);
                        debug_info("[LFI] Error in Send ft ack comm "<<id<<" "<<std::hex<<&send_request<<std::dec);
                        continue;
                    }
                }
                {
                    auto [it, _] = requests.emplace(index++, comm);
                    auto& recv_request = it->second;
                    debug_info("[LFI] Recv ft ack comm "<<id<<" "<<std::hex<<&recv_request<<std::dec);
                    msg = async_recv(&ack, sizeof(ack), LFI_TAG_FT, recv_request, timeout_ms);
                    if (msg.error < 0){
                        comm.ft_error = true;
                        comms_with_err.push_back(id);
                        debug_info("[LFI] Error in Recv ft ack comm "<<id<<" "<<std::hex<<&recv_request<<std::dec);
                        continue;
                    }
                }
            }   
            index = 0;
            auto start = std::chrono::high_resolution_clock::now();
            for (auto &[id, comm] : lfi.m_comms)
            {
                if (comm.rank_peer == ANY_COMM_SHM || comm.rank_peer == ANY_COMM_PEER) continue;
                if (comm.is_canceled || comm.ft_error) continue;
                int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count(); 
    
                int timeout_ms = std::max(0, env::get_instance().LFI_fault_tolerance_time*1000 - elapsed_ms);
                auto& send_request = requests.at(index++);
                debug_info("[LFI] wait ft send ack comm "<<id<<" "<<&send_request);
                ret = wait(send_request, timeout_ms);
                if (ret == - LFI_TIMEOUT){
                    cancel(send_request);
                }
                auto& recv_request = requests.at(index++);
                debug_info("[LFI] wait ft recv ack comm "<<id<<" "<<&recv_request);
                ret = wait(recv_request, timeout_ms);
                if (ret == - LFI_TIMEOUT){
                    cancel(recv_request);
                }
                debug_info("[LFI] wait ft ack comm errors "<<send_request.error<<" "<<recv_request.error);
                if (send_request.error < 0 || recv_request.error < 0){
                    comms_with_err.push_back(id);
                }
            }
            for (auto &id : comms_with_err)
            {
                auto comm = lfi.get_comm(id);
                if (comm == nullptr){
                    print("This should not happen");
                    throw std::runtime_error("Not found comm this should not happen");
                    continue;
                }
                std::unique_lock lock(comm->ft_mutex);
                debug_info("[LFI] cancel all request in comm with error "<<id);
                for(auto &request : comm->ft_requests){
                    if (request == nullptr) continue;
                    debug_info("[LFI] cancel "<<request->to_string());
                    lfi.cancel(*request);
                    debug_info("[LFI] canceled "<<request->to_string());
                }
                comm->ft_requests.clear();
                
                debug_info("[LFI] close comm with error "<<id);
                comm->is_canceled = true;
            }
            comms_with_err.clear();
            
            requests.clear();
            
            ft_lock.lock();
        }

        debug_info("[LFI] End");
        return ret;
    }
} // namespace LFI