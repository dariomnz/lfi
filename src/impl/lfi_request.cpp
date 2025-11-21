
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

#include "impl/lfi_request.hpp"

#include <rdma/fi_errno.h>

#include "impl/debug.hpp"
#include "impl/env.hpp"
#include "impl/lfi.hpp"
#include "impl/profiler.hpp"
#include "lfi_comm.hpp"
#include "lfi_error.h"

namespace LFI {

std::ostream &operator<<(std::ostream &os, lfi_request &req) {
    os << "Request " << (req.is_send ? "send" : "recv");
    if (req.wait_context) {
        os << " ctx " << std::hex << req.wait_context;
    }
    os << std::dec << " comm " << lfi_comm_to_string(req.m_comm.rank_peer) << " {size:" << req.size
       << ", tag:" << lfi_tag_to_string(req.tag) << ", source:" << lfi_comm_to_string(req.source) << "}";
    if (req.is_inject) {
        os << " inject";
    }
    if (req.is_completed()) {
        os << " completed";
    }
    if (req.shared_wait_struct) {
        os << " shared_wait";
    }
    if (req.error) {
        os << " Error: " << lfi_strerror(req.error);
    }
    return os;
}

void lfi_request::reset() {
    LFI_PROFILE_FUNCTION();
    if (wait_context) {
        wait_context->unassign();
    }
    wait_context = nullptr;
    error = 0;
    size = 0;
    tag = 0;
    source = UNINITIALIZED_COMM;
}

void lfi_request::complete(int err) {
    LFI_PROFILE_FUNCTION();

    if (env::get_instance().LFI_fault_tolerance) {
        {
            std::unique_lock ft_lock(m_comm.ft_mutex);
            debug_info("[LFI] erase ft_requests " << this << " in comm " << m_comm.rank_peer);
            m_comm.ft_requests.erase(this);
            {
                std::unique_lock lock(m_comm.m_ep.ft_mutex);
                if (m_comm.rank_peer != ANY_COMM_SHM && m_comm.rank_peer != ANY_COMM_PEER) {
                    m_comm.ft_comm_count = (m_comm.ft_comm_count > 0) ? (m_comm.ft_comm_count - 1) : 0;
                    debug_info("[LFI] ft_comm_count " << m_comm.ft_comm_count);
                    if (m_comm.ft_comm_count == 0) {
                        m_comm.m_ep.ft_comms.erase(&m_comm);
                    }
                } else {
                    debug_info("[LFI] remove of ft_any_comm_requests " << this);
                    m_comm.m_ep.ft_any_comm_requests.erase(this);
                }
            }
        }
        if (!is_send && err == LFI_SUCCESS) {
            // Update time outside request lock
            auto [lock, comm] = m_comm.m_ep.m_lfi.get_comm_and_mutex(source);
            if (comm) {
                std::unique_lock ft_lock(comm->ft_mutex);
                comm->last_request_time = lfi_comm::clock::now();
            }
        }
    }

    std::unique_lock request_lock(mutex);
    debug_info("[LFI] >> Begin complete request " << *this);
    // If completed do nothing
    if (is_completed()) {
        debug_info("[LFI] << End complete request already completed " << *this);
        return;
    }
    error = err;
    if (wait_context) {
        wait_context->unassign();
    }
    wait_context = nullptr;
    cv.notify_all();
    if (shared_wait_struct != nullptr) {
        std::unique_lock shared_wait_lock(shared_wait_struct->wait_mutex);
        debug_info("[LFI] have shared_wait_struct");
        shared_wait_struct->wait_count--;
        if (shared_wait_struct->wait_count <= 0) {
            shared_wait_struct->wait_cv.notify_all();
        }
    }
    if (callback) {
        // Call the callback with the current error of the request
        debug_info("[LFI] register callback on complete for request " << this);
        std::unique_lock callback_lock(m_comm.m_ep.callbacks_mutex);
        m_comm.m_ep.callbacks.emplace_back(callback, error, callback_ctx);
        // Unlock in case the callback free the request when the callback_lock is unlocked
        request_lock.unlock();
    } else {
        debug_info("[LFI] there are no callback to call");
        debug_info("[LFI] << End complete request");
    }
}

void lfi_request::cancel() {
    LFI_PROFILE_FUNCTION();
    lfi_endpoint *ep;
    int error = -LFI_CANCELED;
    {
        std::unique_lock request_lock(mutex);
        debug_info("[LFI] Start " << *this);
        // The inject is not cancelled
        if (is_inject || is_completed()) return;

        ep = &m_comm.m_ep;

        fid_ep *p_ep = nullptr;
        if (is_send) {
            p_ep = m_comm.m_ep.use_scalable_ep ? m_comm.m_ep.tx_ep : m_comm.m_ep.ep;
        } else {
            p_ep = m_comm.m_ep.use_scalable_ep ? m_comm.m_ep.rx_ep : m_comm.m_ep.ep;
        }
        // Cancel request and notify

        // Ignore return value
        // ref: https://github.com/ofiwg/libfabric/issues/7795
        [[maybe_unused]] auto ret = fi_cancel(&p_ep->fid, this);
        debug_info("fi_cancel ret " << ret << " " << fi_strerror(ret));

        // Check if completed to no report error
        if (!is_completed() || error) {
            if (m_comm.is_canceled || error == -LFI_BROKEN_COMM) {
                error = -LFI_BROKEN_COMM;
            } else {
                error = -LFI_CANCELED;
            }
        }
    }

    // This is after complete because complete unassign the context
    // Try one progress to read the canceled and not accumulate errors
    ep->protected_progress(false);

    complete(error);

    debug_info("[LFI] End " << this);
}
}  // namespace LFI