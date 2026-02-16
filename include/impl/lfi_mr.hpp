
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

#include <cstddef>
#include <unordered_map>

#include "rdma/fi_domain.h"
#include "lfi.h"

namespace LFI {
struct lfi_mr {
    void *addr;
    size_t size;
    fid_mr *shm_mr = nullptr;
    fid_mr *peer_mr = nullptr;
};

struct lfi_mr_key_eq {
    bool operator()(const lfi_mr_key& a, const lfi_mr_key& b) const {
        return a.shm_key == b.shm_key && a.peer_key == b.peer_key;
    }
};

}  // namespace LFI
namespace std {
template <>
struct hash<lfi_mr_key> {
    size_t operator()(const lfi_mr_key &k) const {
        size_t h1 = std::hash<int64_t>{}(k.shm_key);
        size_t h2 = std::hash<int64_t>{}(k.peer_key);
        // boost hash conbine
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};
}  // namespace std