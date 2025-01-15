
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
#include "impl/lfi.hpp"
#include "impl/ns.hpp"
#include "impl/socket.hpp"

namespace LFI {

uint32_t LFI::reserve_comm() { return m_rank_counter.fetch_add(1); }

lfi_comm &LFI::init_comm(bool is_shm, int32_t comm_id) {
    if (is_shm) {
        return create_comm(shm_ep, comm_id);
    } else {
        return create_comm(peer_ep, comm_id);
    }
}

lfi_comm &LFI::create_comm(lfi_ep &lfi_ep, int32_t comm_id) {
    uint32_t new_id = comm_id;

    std::unique_lock comms_lock(m_comms_mutex);
    debug_info("[LFI] Start");
    if (comm_id >= 0) {
        if (m_comms.find(comm_id) == m_comms.end()) {
            new_id = comm_id;
        } else {
            throw std::runtime_error("Want to create a comm with a id that exits");
        }
    } else {
        new_id = reserve_comm();
    }
    auto [key, inserted] =
        m_comms.emplace(std::piecewise_construct, std::forward_as_tuple(new_id), std::forward_as_tuple(lfi_ep));
    key->second.rank_peer = new_id;
    debug_info("[LFI] rank_peer " << key->second.rank_peer);
    debug_info("[LFI] End");
    return key->second;
}

lfi_comm &LFI::create_any_comm(lfi_ep &lfi_ep, uint32_t comm_id) {
    uint32_t new_id = comm_id;

    debug_info("[LFI] Start");
    auto [key, inserted] =
        m_comms.emplace(std::piecewise_construct, std::forward_as_tuple(new_id), std::forward_as_tuple(lfi_ep));
    key->second.rank_peer = new_id;
    key->second.rank_self_in_peer = new_id;
    key->second.is_ready = true;
    debug_info("[LFI] rank_peer " << key->second.rank_peer);
    debug_info("[LFI] End");
    return key->second;
}

lfi_comm *LFI::get_comm(uint32_t id) {
    debug_info("[LFI] Start " << id);
    std::unique_lock comms_lock(m_comms_mutex);
    auto it = m_comms.find(id);
    if (it == m_comms.end() || (it != m_comms.end() && !it->second.is_ready)) {
        // If fail or not ready check if is in fut comm and retry
        std::unique_lock fut_lock(m_fut_mutex);

        auto fut_it = m_fut_comms.find(id);
        if (fut_it == m_fut_comms.end()) {
            debug_info("[LFI] End " << id << " not found in futs");
            return nullptr;
        }

        auto &fut = fut_it->second;

        if (fut.valid()) {
            fut_lock.unlock();
            comms_lock.unlock();
            fut.get();
            comms_lock.lock();
            fut_lock.lock();
        }

        m_fut_comms.erase(id);

        // Retry search
        it = m_comms.find(id);
        if (it == m_comms.end()) {
            debug_info("[LFI] End " << id << " not found in comms nor futs");
            return nullptr;
        }
    }

    debug_info("[LFI] End " << id << " found");
    return &it->second;
}

int LFI::close_comm(uint32_t id) {
    int ret = 0;
    debug_info("[LFI] Start");

    lfi_comm *comm = get_comm(id);
    if (comm == nullptr) {
        return -1;
    }

    remove_addr(*comm);

    std::unique_lock comms_lock(m_comms_mutex);
    m_comms.erase(comm->rank_peer);

    debug_info("[LFI] End = " << ret);

    return ret;
}
}  // namespace LFI