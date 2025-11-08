
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

#include <chrono>
#include <thread>

#include "impl/debug.hpp"
#include "impl/env.hpp"
#include "impl/lfi.hpp"
#include "lfi_comm.hpp"
#include "lfi_error.h"
#include "sstream"

namespace LFI {

std::ostream &operator<<(std::ostream &os, lfi_request &req) {
    os << "Request " << (req.is_send ? "send " : "recv ");
    os << std::hex << &req;
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

void lfi_request::complete(int err) {
    std::unique_lock request_lock(mutex);
    debug_info("[LFI] >> Begin complete request " << *this);
    // If completed do nothing
    if (is_completed()) {
        debug_info("[LFI] << End complete request already completed " << *this);
        return;
    }
    error = err;
    wait_context = false;
    cv.notify_all();
    if (shared_wait_struct != nullptr) {
        std::unique_lock shared_wait_lock(shared_wait_struct->wait_mutex);
        debug_info("[LFI] have shared_wait_struct");
        shared_wait_struct->wait_count--;
        if (shared_wait_struct->wait_count <= 0) {
            shared_wait_struct->wait_cv.notify_all();
        }
    }

    auto aux_callback = callback;
    auto &comm = m_comm;
    request_lock.unlock();
    if (env::get_instance().LFI_fault_tolerance) {
        {
            std::unique_lock ft_lock(comm.ft_mutex);
            debug_info("[LFI] erase ft_requests " << this << " in comm " << comm.rank_peer);
            comm.ft_requests.erase(this);

            if (comm.rank_peer != ANY_COMM_SHM && comm.rank_peer != ANY_COMM_PEER) {
                std::unique_lock lock(comm.m_ep.ft_comms_mutex);
                comm.ft_comm_count = comm.ft_comm_count > 0 ? comm.ft_comm_count - 1 : 0;
                debug_info("[LFI] ft_comm_count " << comm.ft_comm_count);
                if (comm.ft_comm_count == 0) {
                    comm.m_ep.ft_comms.erase(&comm);
                }
            } else {
                std::unique_lock lock(comm.m_ep.ft_any_comm_requests_mutex);
                debug_info("[LFI] remove of ft_any_comm_requests " << this);
                comm.m_ep.ft_any_comm_requests.erase(this);
            }
        }
    }
    if (aux_callback) {
        // Call the callback with the current error of the request
        debug_info("[LFI] calling callback on complete for request " << this);
        aux_callback(error);
        // Maybe the callback free the request
    } else {
        debug_info("[LFI] there are no callback to call");
        debug_info("[LFI] << End complete request");
    }
}

void lfi_request::cancel() {
    {
        std::unique_lock request_lock(mutex);
        debug_info("[LFI] Start " << *this);
        // The inject is not cancelled
        if (is_inject || is_completed()) return;

        fid_ep *p_ep = nullptr;
        if (is_send) {
            p_ep = m_comm.m_ep.use_scalable_ep ? m_comm.m_ep.tx_ep : m_comm.m_ep.ep;
        } else {
            p_ep = m_comm.m_ep.use_scalable_ep ? m_comm.m_ep.rx_ep : m_comm.m_ep.ep;
        }
        // Cancel request and notify

        // Ignore return value
        // ref: https://github.com/ofiwg/libfabric/issues/7795
        auto ret = fi_cancel(&p_ep->fid, this);
        debug_info("fi_cancel ret " << ret << fi_strerror(ret));
    }

    if (!is_send) {
        m_comm.m_ep.m_lfi.wait(*this, 1000);
    }

    // Try one progress to read the canceled and not accumulate errors
    // if (!m_comm.m_ep.protected_progress()) {
    //     std::this_thread::sleep_for(std::chrono::microseconds(100));
    //     // std::this_thread::yield();
    // }

    // Check if completed to no report error
    int error = -LFI_CANCELED;
    {
        std::unique_lock request_lock(mutex);
        if (!is_completed() || error) {
            if (m_comm.is_canceled || error == -LFI_BROKEN_COMM) {
                error = -LFI_BROKEN_COMM;
            } else {
                error = -LFI_CANCELED;
            }
        }
    }
    complete(error);

    debug_info("[LFI] End " << this);
}
}  // namespace LFI