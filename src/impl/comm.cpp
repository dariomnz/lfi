
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

// uint32_t LFI::reserve_comm() { return m_rank_counter.fetch_add(1); }

lfi_comm* LFI::init_comm(bool is_shm, int32_t comm_id) {
    LFI_PROFILE_FUNCTION();
    if (is_shm && env::get_instance().LFI_use_shm) {
        return init_comm(shm_ep, comm_id);
    } else {
        return init_comm(peer_ep, comm_id);
    }
}

uint32_t LFI::reserve_comm() {
    LFI_PROFILE_FUNCTION();
    std::unique_lock comms_lock(m_comms_mutex);
    debug_info("[LFI] Start");
    auto comm_id = m_rank_counter.fetch_add(1);
    m_comms.emplace(std::piecewise_construct, std::forward_as_tuple(comm_id), std::forward_as_tuple(nullptr));
    debug_info("[LFI] rank_peer " << comm_id);
    debug_info("[LFI] End");
    return comm_id;
}

lfi_comm* LFI::init_comm(lfi_endpoint& lfi_ep, int32_t comm_id) {
    LFI_PROFILE_FUNCTION();
    uint32_t new_id = comm_id;

    std::unique_lock comms_lock(m_comms_mutex);
    debug_info("[LFI] Start");
    auto it = m_comms.find(comm_id);
    if (it == m_comms.end()) {
        print("[LFI] [ERROR] internal error call to init_comm with comm that is not reserved " << comm_id);
        std::terminate();
    } else if (it != m_comms.end() && it->second != nullptr) {
        print("[LFI] [ERROR] internal error call to init_comm with comm that is already initialized " << comm_id);
        std::terminate();
    }

    it->second = std::make_unique<lfi_comm>(lfi_ep);
    it->second->rank_peer = new_id;
    debug_info("[LFI] rank_peer " << it->second->rank_peer);
    debug_info("[LFI] End");
    return it->second.get();
}

lfi_comm* LFI::create_any_comm(lfi_endpoint& lfi_ep, uint32_t comm_id) {
    LFI_PROFILE_FUNCTION();
    uint32_t new_id = comm_id;

    std::unique_lock comms_lock(m_comms_mutex);
    debug_info("[LFI] Start");
    auto [key, inserted] = m_comms.emplace(std::piecewise_construct, std::forward_as_tuple(new_id),
                                           std::forward_as_tuple(std::make_unique<lfi_comm>(lfi_ep)));
    key->second->rank_peer = new_id;
    key->second->rank_self_in_peer = new_id;
    key->second->is_ready = lfi_comm::comm_status::READY;
    debug_info("[LFI] rank_peer " << key->second->rank_peer);
    debug_info("[LFI] End");
    return key->second.get();
}

std::pair<std::shared_lock<std::shared_mutex>, lfi_comm*> LFI::get_comm_and_mutex(uint32_t id) {
    LFI_PROFILE_FUNCTION();
    debug_info("[LFI] Start " << id);
    std::shared_lock comms_lock(m_comms_mutex);

    lfi_comm* comm = get_comm_internal(comms_lock, id);

    return {std::move(comms_lock), comm};
}

lfi_comm* LFI::get_comm(uint32_t id) {
    LFI_PROFILE_FUNCTION();
    debug_info("[LFI] Start " << id);
    std::shared_lock comms_lock(m_comms_mutex);
    lfi_comm* comm = get_comm_internal(comms_lock, id);

    return comm;
}

lfi_comm* LFI::get_comm_internal(std::shared_lock<std::shared_mutex>& comms_lock, uint32_t id) {
    LFI_PROFILE_FUNCTION();
    // debug_info("[LFI] Start " << id);
    auto comm_it = m_comms.find(id);
    if (comm_it == m_comms.end()) {
        debug_info("[LFI] End " << id << " not found in comm so it is nor reserved");
        return nullptr;
    }

    // Not necesary to wait when is ready
    if (comm_it->second != nullptr && comm_it->second->is_ready != lfi_comm::comm_status::NOT_READY) {
        debug_info("[LFI] End comm " << id << " is ready");
        return comm_it->second.get();
    }
    decltype(m_fut_comms.extract(id)) fut_it;
    {
        std::unique_lock fut_lock(m_fut_comms_mutex);
        fut_it = m_fut_comms.extract(id);
    }
    if (fut_it.empty()) {
        // There are no fut for the comm so wait to the resolution of the fut in another thread
        if (comm_it->second == nullptr ||
            (comm_it->second != nullptr && comm_it->second->is_ready != lfi_comm::comm_status::READY)) {
            debug_info("[LFI] wait comm " << id << " not ready");
            m_fut_wait_cv.wait(comms_lock, [&]() {
                return comm_it->second != nullptr && comm_it->second->is_ready == lfi_comm::comm_status::READY;
            });
        }

        debug_info("[LFI] End comm " << id << " ready");
        return comm_it->second.get();
    }

    // Wait the fut, next wake up the other threads
    if (fut_it.mapped().valid()) {
        debug_info("[LFI] Need to wait for the connection to end");
        comms_lock.unlock();
        fut_it.mapped().get();
        comms_lock.lock();
        debug_info("[LFI] Finish waiting for the connection");
    }
    m_fut_wait_cv.notify_all();
    debug_info("[LFI] End comm " << id << " found after wait fut");
    return comm_it->second.get();
}

int LFI::close_comm(uint32_t id) {
    LFI_PROFILE_FUNCTION();
    int ret = 0;
    debug_info("[LFI] Start");
    {
        auto [lock, comm] = get_comm_and_mutex(id);
        if (!comm) {
            return -LFI_COMM_NOT_FOUND;
        }
        {
            std::unique_lock lock(comm->ft_mutex);
            debug_info("[LFI] cancel all request in comm with error " << comm->rank_peer);
            // In a temp set because cancel call complete that erase the request from ft_requests
            std::unordered_set<lfi_request*> temp_requests(comm->ft_requests);
            for (auto& request : temp_requests) {
                if (request == nullptr) continue;
                if (id != ANY_COMM_SHM && id != ANY_COMM_PEER && request->tag != LFI_TAG_FT_PING &&
                    request->tag != LFI_TAG_FT_PONG) {
                    print("[LFI] [WARNING] Closing comm with pending request: " << *request);
                }
                request->cancel();
            }
            comm->ft_requests.clear();
        }

        remove_addr(*comm);
        {
            std::unique_lock lock(comm->m_endpoint.ft_mutex);
            comm->m_endpoint.ft_comms.erase(comm);
        }
    }
    {
        std::unique_lock comm_lock(m_comms_mutex);
        // Here comm is deleted
        m_comms.erase(id);
    }

    debug_info("[LFI] End = " << ret);

    return ret;
}
}  // namespace LFI