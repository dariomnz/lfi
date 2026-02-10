
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
#include <cstdint>
#include <ostream>

#include "rdma/fi_endpoint.h"
namespace LFI {

struct lfi_pending_op {
    enum class Type : uint8_t { NONE, SEND, SENDV, RECV, RECVV, INJECT };
    Type type;
    fid_ep *ep;
    union {
        const void *cbuf;
        void *buf;
    } buf;
    size_t len;
    fi_addr_t addr;
    uint64_t tag;
    uint64_t ignore;
    void *context;

    friend std::ostream &operator<<(std::ostream &os, const lfi_pending_op &op) {
#define CASE_TYPE(type)              \
    case lfi_pending_op::Type::type: \
        os << #type " ";             \
        break;
        os << "PendingOp ";
        switch (op.type) {
            CASE_TYPE(NONE);
            CASE_TYPE(SEND);
            CASE_TYPE(SENDV);
            CASE_TYPE(RECV);
            CASE_TYPE(RECVV);
            CASE_TYPE(INJECT);
        }
        os << "{len:" << op.len << ", tag:" << op.tag << ", addr:" << op.addr << ", context:" << op.context << "}";
        return os;
    }
};

}  // namespace LFI