
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
#include "impl/progress_guard.hpp"

#include "impl/debug.hpp"
#include "impl/lfi_endpoint.hpp"

namespace LFI {

std::mutex& ProgressGuard::get_mutex() { return m_ep.waiters_mutex; }

bool ProgressGuard::try_acquire() {
    if (m_requested && !m_is_leader) {
        // printf("before in_progress %s\n", (m_ep.in_progress ? "true" : "false"));
        m_is_leader = !m_ep.in_progress.exchange(true);
        // printf("after in_progress %s\n", (m_ep.in_progress ? "true" : "false"));
        debug_info("try_acquire in " << this << " " << (m_is_leader ? "true" : "false"));
    }

    return m_is_leader;
}

void ProgressGuard::register_waiter() {
    std::unique_lock lock(m_ep.waiters_mutex);
    m_ep.waiters_list.push_back(this);
    debug_info("Emplace progress guard " << this);
}

void ProgressGuard::unregister_waiter() {
    std::unique_lock lock(m_ep.waiters_mutex);
    m_ep.waiters_list.erase(this);
    debug_info("Erase progress guard " << this);
}

void ProgressGuard::transfer_leader_and_wake() {
    if (!m_is_leader) return;

    std::unique_lock lock(m_ep.waiters_mutex);

    ProgressGuard* successor = m_ep.waiters_list.head;
    if (successor) {
        this->m_is_leader = false;
        successor->m_is_leader = true;
        if (successor->m_mutex && successor->m_cv) {
            std::unique_lock successor_lock(*successor->m_mutex);
            successor->m_cv->notify_one();
        }
    } else {
        m_ep.in_progress.store(false);
        m_is_leader = false;
    }
}
}  // namespace LFI