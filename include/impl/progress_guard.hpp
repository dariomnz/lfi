
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

#include <atomic>
#include <condition_variable>

#include "env.hpp"
#include "ft_manager.hpp"

namespace LFI {

// Forward declaration
struct lfi_endpoint;
// RAII helper to manage progress leadership for an endpoint
class ProgressGuard {
    lfi_endpoint &m_ep;
    std::mutex *m_mutex = nullptr;
    std::condition_variable *m_cv = nullptr;
    std::atomic<bool> m_is_leader = false;
    bool m_requested = false;

   public:
    ProgressGuard *prev = nullptr;
    ProgressGuard *next = nullptr;
    bool inserted = false;

   public:
    inline ProgressGuard(lfi_endpoint &ep, std::mutex *mutex = nullptr, std::condition_variable *cv = nullptr,
                         bool requested = true)
        : m_ep(ep), m_mutex(mutex), m_cv(cv), m_requested(requested) {
        if (!env::get_instance().LFI_efficient_progress) {
            m_is_leader = m_requested;
            return;
        }
        try_acquire();
        register_waiter();
    }

    inline ~ProgressGuard() {
        if (!env::get_instance().LFI_efficient_progress) {
            return;
        }
        unregister_waiter();
        transfer_leader_and_wake();
    }

    std::mutex &get_mutex();

    inline bool leader() { return m_is_leader; }

   private:
    void transfer_leader_and_wake();
    bool try_acquire();
    void register_waiter();
    void unregister_waiter();
};

}  // namespace LFI