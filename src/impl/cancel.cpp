
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
#include "sstream"

namespace LFI {

int LFI::cancel(lfi_request &request) {
    // The inject is not cancelled
    debug_info("[LFI] Start " << request.to_string());
    {
        std::unique_lock request_lock(request.mutex);
        if (request.is_inject || request.is_completed()) return LFI_SUCCESS;
    }

    fid_ep *p_ep = nullptr;
    if (request.is_send) {
        p_ep = request.m_comm->m_ep.use_scalable_ep ? request.m_comm->m_ep.tx_ep : request.m_comm->m_ep.ep;
    } else {
        p_ep = request.m_comm->m_ep.use_scalable_ep ? request.m_comm->m_ep.rx_ep : request.m_comm->m_ep.ep;
    }
    // Cancel request and notify
    int ret = 0;

    // Ignore return value
    // ref: https://github.com/ofiwg/libfabric/issues/7795
    fi_cancel(&p_ep->fid, &request);
    debug_info("fi_cancel ret " << ret << " " << fi_strerror(ret));

    if (request.is_send == true) {
        // Try one progress to read the canceled and not accumulate errors
        std::unique_lock ep_lock(request.m_comm->m_ep.mutex_ep, std::defer_lock);
        if (ep_lock.try_lock()) {
            progress(request.m_comm->m_ep);
            ep_lock.unlock();
        }

        // Check if completed to no report error
        if (request.wait_context) {
            std::unique_lock request_lock(request.mutex);
            request.wait_context = false;
            request.error = -LFI_CANCELED;
            request.cv.notify_all();
        }
    } else {
        // For the recvs wait to the completion of the cancel
        ret = wait(request);
    }

    debug_info("[LFI] End " << std::hex << &request << std::dec);
    return ret;
}
}  // namespace LFI