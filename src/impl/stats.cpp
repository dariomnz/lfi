
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

#include <cstring>

#include "impl/debug.hpp"
#include "impl/env.hpp"
#include "impl/lfi.hpp"
#include "impl/profiler.hpp"
#include "lfi_comm.hpp"

namespace LFI {

struct indent {
    int m_level = 0;
    indent(int level) : m_level(level) {}

    friend std::ostream &operator<<(std::ostream &os, const indent &ind) {
        for (int i = 0; i < ind.m_level; i++) {
            os << "    ";
        }
        return os;
    }
};

void LFI::dump_stats() {
    LFI_PROFILE_FUNCTION();
    auto now = std::chrono::high_resolution_clock::now();
    std::cerr << "-------------------------------[LFI STATS BEGIN] "
              << format_time<std::chrono::high_resolution_clock>(now) << "-------------------------------" << std::endl;
    {
        std::shared_lock lock(m_comms_mutex);
        std::cerr << "Comms size " << m_comms.size() << std::endl;
        for (auto &&[key, comm] : m_comms) {
            if (!comm) {
                std::cerr << indent(1) << "Comm " << format_lfi_comm{key} << " nullptr" << std::endl;
                continue;
            }
            std::cerr << indent(1) << "Comm " << format_lfi_comm{key} << (comm->m_endpoint.is_shm ? " SHM" : " PEER")
                      << std::endl;
            if (env::get_instance().LFI_fault_tolerance) {
                std::unique_lock ft_comm_lock(comm->ft_mutex);
                std::cerr << indent(2) << "Last request: " << format_time<lfi_comm::clock>(comm->last_request_time)
                          << std::endl;
                std::cerr << indent(2) << "ft_requests size: " << comm->ft_requests.size() << std::endl;
                for (auto &&req : comm->ft_requests) {
                    std::unique_lock lock_req(req->mutex);
                    std::cerr << indent(3) << *req << std::endl;
                }
                std::cerr << indent(2) << "ft_current_status: " << format_ft_status{comm->ft_current_status};
            }
        }
    }
    auto dump_endpoint = [&](lfi_endpoint &endpoint) {
        std::cerr << "Endpoint " << (endpoint.is_shm ? "SHM" : "PEER")
                  << " provider: " << endpoint.info->fabric_attr->prov_name << std::endl;

        {
            std::unique_lock lock(endpoint.ft_mutex);
            std::cerr << indent(1) << "ft_any_comm_requests size: " << endpoint.ft_any_comm_requests.size()
                      << std::endl;
            for (auto &&req : endpoint.ft_any_comm_requests) {
                std::unique_lock lock_req(req->mutex);
                std::cerr << indent(2) << *req << std::endl;
            }
        }
        {
            std::unique_lock lock(endpoint.ft_mutex);
            std::cerr << indent(1) << "ft_pending_failed_comms size: " << endpoint.ft_pending_failed_comms.size()
                      << std::endl;
            for (auto &&comm : endpoint.ft_pending_failed_comms) {
                std::cerr << indent(2) << "Comm " << format_lfi_comm{comm} << std::endl;
            }
        }
        {
            std::unique_lock lock(endpoint.ft_mutex);
            std::cerr << indent(1) << "ft_comms size: " << endpoint.ft_comms.size() << std::endl;
            for (auto &&comm : endpoint.ft_comms) {
                std::cerr << indent(2) << "Comm " << comm->rank_peer << std::endl;
            }
        }

        {
            std::unique_lock lock(endpoint.waiters_mutex);
            std::cerr << indent(1) << "num_waiters: " << endpoint.waiters_list.size() << std::endl;
            for (auto &&waiter : endpoint.waiters_list) {
                std::cerr << indent(2) << "Waiter " << waiter << std::endl;
            }
        }
    };

    dump_endpoint(shm_ep);
    dump_endpoint(peer_ep);

    req_ctx_factory.dump();
    std::cerr << "--------------------------------[LFI STATS END] "
              << format_time<std::chrono::high_resolution_clock>(now) << "--------------------------------"
              << std::endl;
}

}  // namespace LFI