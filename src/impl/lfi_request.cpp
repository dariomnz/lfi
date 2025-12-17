
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
    auto aux_wait_context = req.wait_context.load();
    if (aux_wait_context) {
        os << " ctx " << std::hex << aux_wait_context;
    }
    os << std::dec << " comm " << lfi_comm_to_string(req.m_comm_id) << " {size:" << req.size
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
    auto aux_wait_context = wait_context.load();
    if (aux_wait_context) {
        aux_wait_context->unassign();
    }
    error = 0;
    size = 0;
    tag = 0;
    source = UNINITIALIZED_COMM;
    wait_context.store(nullptr);
}

void lfi_request::complete(int err) {
    LFI_PROFILE_FUNCTION();

    if (env::get_instance().LFI_fault_tolerance) {
        auto [lock, comm] = m_endpoint.m_lfi.get_comm_and_mutex(m_comm_id);
        if (comm) {
            {
                std::unique_lock ft_lock(comm->ft_mutex);
                debug_info("[LFI] erase ft_requests " << this << " in comm " << m_comm_id);
                comm->ft_requests.erase(this);
                {
                    std::unique_lock lock(m_endpoint.ft_mutex);
                    if (m_comm_id != ANY_COMM_SHM && m_comm_id != ANY_COMM_PEER) {
                        comm->ft_comm_count = (comm->ft_comm_count > 0) ? (comm->ft_comm_count - 1) : 0;
                        debug_info("[LFI] ft_comm_count " << comm->ft_comm_count);
                        if (comm->ft_comm_count == 0) {
                            m_endpoint.ft_comms.erase(comm);
                        }
                    } else {
                        debug_info("[LFI] remove of ft_any_comm_requests " << this);
                        m_endpoint.ft_any_comm_requests.erase(this);
                    }
                }
            }
        }
        if (!is_send && err == LFI_SUCCESS) {
            // Update time outside request lock
            auto comm = m_endpoint.m_lfi.get_comm_internal(lock, source);
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
    auto aux_wait_context=wait_context.load();
    if (aux_wait_context) {
        aux_wait_context->unassign();
    }
    wait_context.store(nullptr);
    cv.notify_all();
    if (shared_wait_struct != nullptr) {
        std::unique_lock shared_wait_lock(shared_wait_struct->wait_mutex);
        debug_info("[LFI] have shared_wait_struct");
        auto wait_count = shared_wait_struct->wait_count.fetch_sub(1);
        if (wait_count <= 0) {
            shared_wait_struct->wait_cv.notify_all();
        }
    }
    if (callback) {
        // Call the callback with the current error of the request
        debug_info("[LFI] register callback on complete for request " << this);
        std::unique_lock callback_lock(m_endpoint.callbacks_mutex);
        m_endpoint.callbacks.emplace_back(callback, error, callback_ctx);
        // Unlock in case the callback free the request when the callback_lock is unlocked
        request_lock.unlock();
    } else {
        debug_info("[LFI] there are no callback to call");
        debug_info("[LFI] << End complete request");
    }
}

void lfi_request::cancel() {
    LFI_PROFILE_FUNCTION();
    int error = -LFI_CANCELED;
    {
        std::unique_lock request_lock(mutex);
        debug_info("[LFI] Start " << *this);
        // The inject is not cancelled
        if (is_inject || is_completed()) return;

        fid_ep *p_ep = nullptr;
        if (is_send) {
            p_ep = m_endpoint.tx_endpoint();
        } else {
            p_ep = m_endpoint.rx_endpoint();
        }
        // Cancel request and notify

        // Ignore return value
        // ref: https://github.com/ofiwg/libfabric/issues/7795
        auto aux_context = wait_context.load();
        if (aux_context) {
            [[maybe_unused]] auto ret = fi_cancel(&p_ep->fid, aux_context);
            debug_info("fi_cancel ret " << ret << " " << fi_strerror(ret));
        }

        // Check if completed to no report error
        if (!is_completed() || error) {
            auto [lock, comm] = m_endpoint.m_lfi.get_comm_and_mutex(m_comm_id);
            if ((comm && comm->is_canceled) || error == -LFI_BROKEN_COMM) {
                error = -LFI_BROKEN_COMM;
            } else {
                error = -LFI_CANCELED;
            }
        }
    }

    // This is after complete because complete unassign the context
    // Try one progress to read the canceled and not accumulate errors
    m_endpoint.protected_progress(false);

    complete(error);

    debug_info("[LFI] End " << this);
}
}  // namespace LFI