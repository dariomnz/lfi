
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

    int LFI::init_thread_cq()
    {
        LFI &lfi = LFI::get_instance();
        if (!lfi.have_thread || lfi.threads_cq.size() != 0)
            return 0;

        debug_info("[LFI] Start");
        lfi.threads_cq = std::vector<thread_cq>(env::get_instance().LFI_threads);

        for (size_t i = 0; i < lfi.threads_cq.size(); i++)
        {
            lfi.threads_cq[i].id = std::thread([i]()
                                               { run_thread_cq(i); });
        }
        debug_info("[LFI] End");
        return 0;
    }

    int LFI::destroy_thread_cq()
    {
        LFI &lfi = LFI::get_instance();
        if (!lfi.have_thread || lfi.threads_cq.size() == 0)
            return 0;

        debug_info("[LFI] Start");

        for (size_t i = 0; i < lfi.threads_cq.size(); i++)
        {
            auto &t = lfi.threads_cq[i];
            {
                std::lock_guard<std::mutex> lock(t.thread_cq_mutex);
                t.thread_cq_is_running = false;
            }
            t.thread_cq_cv.notify_one();
            t.id.join();
        }

        lfi.threads_cq.clear();

        debug_info("[LFI] End");
        return 0;
    }

    int LFI::progress(fabric_ep &fabric_ep)
    {
        int ret;
        const int comp_count = 8;
        struct fi_cq_tagged_entry comp[comp_count] = {};

        // Libfabric progress
        ret = fi_cq_read(fabric_ep.cq, comp, comp_count);
        if (ret == -FI_EAGAIN)
        {
            return 0;
        }

        // TODO: handle error
        if (ret < 0)
        {
            return ret;
        }

        // Handle the cq entries
        for (int i = 0; i < ret; i++)
        {
            fabric_context *context = static_cast<fabric_context *>(comp[i].op_context);
            fabric_comm* comm = get_comm(context->rank);
            if (comm == nullptr){
                continue;
            }
            context->entry = comp[i];

            {
                std::unique_lock<std::mutex> lock(comm->comm_mutex);
                if (comp[i].flags & FI_SEND)
                {
                    debug_info("[LFI] Send cq of rank_peer " << context->rank);
                }
                if (comp[i].flags & FI_RECV)
                {
                    debug_info("[LFI] Recv cq of rank_peer " << context->rank);
                }

                // print_fi_cq_err_entry(comp);
                // fabric_ep.subs_to_wait--;
                comm->wait_context = false;
                comm->comm_cv.notify_one();
            }
        }
        return ret;
    }

    int LFI::run_thread_cq(uint32_t id)
    {
        int ret = 0;
        LFI &lfi = LFI::get_instance();
        auto &t = lfi.threads_cq[id];
        std::unique_lock<std::mutex> lock(t.thread_cq_mutex);
        debug_info("[LFI] Start");

        while (t.thread_cq_is_running)
        {
            if (t.thread_cq_cv.wait_for(lock, std::chrono::nanoseconds(1), [&t]
                                        { return !t.thread_cq_is_running; }))
            {
                break;
            }

            if (lfi.shm_ep.initialized())
            {
                progress(lfi.shm_ep);
            }
            if (lfi.peer_ep.initialized())
            {
                progress(lfi.peer_ep);
            }
        }

        debug_info("[LFI] End");
        return ret;
    }
} // namespace LFI