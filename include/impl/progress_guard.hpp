
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
    bool m_is_leader = false;
    bool m_requested = false;

   public:
    inline ProgressGuard(lfi_endpoint &ep, std::mutex *mutex = nullptr, std::condition_variable *cv = nullptr,
                         bool requested = true)
        : m_ep(ep), m_mutex(mutex), m_cv(cv), m_requested(requested) {
        register_waiter();
    }

    inline ~ProgressGuard() {
        unregister_waiter();
        release();
    }

    std::mutex &get_mutex();

    bool try_acquire();
    void release();

   private:
    void register_waiter();
    void unregister_waiter();
    void wake_up_one_waiter();
};

}  // namespace LFI