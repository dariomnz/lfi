
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

#pragma once

#include "env.hpp"
#include "lfi.hpp"
#include "lfi_endpoint.hpp"

namespace LFI {

// RAII helper to manage progress leadership for an endpoint
class ProgressGuard {
    lfi_endpoint &m_ep;
    std::condition_variable *m_cv = nullptr;
    bool m_is_leader = false;
    bool m_requested = false;

   public:
    inline ProgressGuard(lfi_endpoint &ep, std::condition_variable *cv = nullptr, bool requested = true)
        : m_ep(ep), m_cv(cv), m_requested(requested) {
        try_acquire();
        register_waiter();
    }

    inline ~ProgressGuard() {
        release();
        unregister_waiter();
    }

    inline void try_acquire() {
        if (m_requested && !m_is_leader) {
            m_is_leader = !env::get_instance().LFI_efficient_progress || !m_ep.in_progress.exchange(true);
        }
    }

    inline void release() {
        if (m_is_leader) {
            m_ep.in_progress.store(false);
            m_is_leader = false;
            wake_up_one_waiter();
        }
    }

    inline bool is_leader() const { return m_is_leader; }

   private:
    inline void register_waiter() {
        if (m_cv != nullptr) {
            std::unique_lock lock(m_ep.waiters_mutex);
            m_ep.waiters_list.emplace(m_cv);
        }
    }
    inline void unregister_waiter() {
        if (m_cv != nullptr) {
            std::unique_lock lock(m_ep.waiters_mutex);
            m_ep.waiters_list.erase(m_cv);
        }
    }
    inline void wake_up_one_waiter() {
        std::unique_lock lock(m_ep.waiters_mutex);
        if (!m_ep.waiters_list.empty()) {
            (*m_ep.waiters_list.begin())->notify_one();
        } else {
            // Wake the ping pong thread if no one is waiting
            m_ep.m_lfi.ft_cv.notify_one();
        }
    }
};

}  // namespace LFI