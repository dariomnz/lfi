
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
#include "impl/profiler.hpp"

namespace LFI {

int LFI::get_addr(lfi_comm& lfi_comm, std::vector<uint8_t>& out_addr) {
    LFI_PROFILE_FUNCTION();
    int ret = -1;
    debug_info("[LFI] Start");

    size_t size_addr = 0;
    ret = fi_getname(&lfi_comm.m_ep.ep->fid, out_addr.data(), &size_addr);
    if (ret != -FI_ETOOSMALL) {
        printf("fi_getname error %d\n", ret);
        return ret;
    }
    debug_info("[LFI] size_addr " << size_addr);
    out_addr.resize(size_addr);
    ret = fi_getname(&lfi_comm.m_ep.ep->fid, out_addr.data(), &size_addr);
    if (ret) {
        printf("fi_getname error %d\n", ret);
        return ret;
    }
    debug_info("[LFI] End = " << ret);
    return ret;
}

int LFI::register_addr(lfi_comm& lfi_comm, std::vector<uint8_t>& addr) {
    LFI_PROFILE_FUNCTION();
    int ret = -1;
    fi_addr_t fi_addr;
    debug_info("[LFI] Start");
    ret = fi_av_insert(lfi_comm.m_ep.av, addr.data(), 1, &fi_addr, 0, NULL);
    if (ret != 1) {
        printf("av insert error %d\n", ret);
        return ret;
    }

    lfi_comm.fi_addr = fi_addr;
    debug_info("[LFI] register fi_addr = " << fi_addr);

    debug_info("[LFI] End = " << ret);
    return ret;
}

int LFI::remove_addr(lfi_comm& lfi_comm) {
    LFI_PROFILE_FUNCTION();
    int ret = -1;
    debug_info("[LFI] Start");

    debug_info("[LFI] remove fi_addr = " << lfi_comm.fi_addr);
    ret = fi_av_remove(lfi_comm.m_ep.av, &lfi_comm.fi_addr, 1, 0);
    if (ret != FI_SUCCESS) {
        print("av remove error " << ret << " " << fi_strerror(ret));
        return ret;
    }
    debug_info("[LFI] End = " << ret);
    return ret;
}
}  // namespace LFI