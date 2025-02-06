
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

#include "lfi.h"

#include "impl/debug.hpp"
#include "impl/env.hpp"
#include "impl/lfi.hpp"
#include "impl/socket.hpp"
#include "lfi_async.h"

#ifdef __cplusplus
extern "C" {
#endif

int lfi_server_create(const char *serv_addr, int *port) {
    int ret = -1;
    std::string addr = "";
    if (serv_addr != nullptr) addr = serv_addr;
    debug_info("(" << addr << ", " << port << ")>> Begin");
    ret = LFI::socket::server_init(addr, *port);
    debug_info("(" << addr << ", " << port << ")=" << ret << " >> End");
    return ret;
}

int lfi_client_create(const char *serv_addr, int port) {
    int out = -1;
    int client_socket;
    debug_info("(" << serv_addr << ", " << port << ")>> Begin");

    client_socket = LFI::socket::client_init(serv_addr, port);
    if (client_socket < 0) {
        print_error("socket::client_init (" << serv_addr << ", " << port << ")");
    }
    LFI::LFI &lfi = LFI::LFI::get_instance();

    out = lfi.reserve_comm();

    auto func = [client_socket, out, &lfi]() {
        int ret = lfi.init_client(client_socket, out);
        uint32_t out = 0;
        if (ret >= 0) {
            out = ret;
        }
        // TODO handle error in close
        LFI::socket::close(client_socket);
        return out;
    };

    if (LFI::env::get_instance().LFI_async_connection) {
        std::unique_lock fut_lock(lfi.m_fut_mutex);
        lfi.m_fut_comms.emplace(out, std::async(std::launch::async, func));
    } else {
        func();
    }

    debug_info("(" << serv_addr << ", " << port << ")=" << out << " >> End");
    return out;
}

int lfi_server_accept(int socket) {
    uint32_t out = -1;
    int client_socket;
    debug_info("(" << socket << ") >> Begin");

    client_socket = LFI::socket::accept(socket);
    if (client_socket < 0) {
        print_error("socket::client_init (" << socket << ")");
    }
    LFI::LFI &lfi = LFI::LFI::get_instance();

    out = lfi.reserve_comm();
    auto func = [client_socket, out, &lfi]() {
        int ret = lfi.init_server(client_socket, out);
        uint32_t out = 0;
        if (ret >= 0) {
            out = ret;
        }
        // TODO handle error in close
        LFI::socket::close(client_socket);
        return out;
    };

    if (LFI::env::get_instance().LFI_async_connection) {
        std::unique_lock fut_lock(lfi.m_fut_mutex);
        lfi.m_fut_comms.emplace(out, std::async(std::launch::async, func));
    } else {
        func();
    }

    debug_info("(" << socket << ")=" << out << " >> End");
    return out;
}

ssize_t lfi_send(int id, const void *data, size_t size) { return lfi_tsend(id, data, size, 0); }

ssize_t lfi_tsend(int id, const void *data, size_t size, int tag) {
    ssize_t ret = -1;
    LFI::lfi_msg msg;
    debug_info("(" << id << ", " << data << ", " << size << ", " << tag << ")>> Begin");
    LFI::LFI &lfi = LFI::LFI::get_instance();
    msg = lfi.send(id, data, size, tag);
    if (msg.error < 0) {
        ret = msg.error;
    } else {
        ret = msg.size;
    }
    debug_info("(" << id << ", " << data << ", " << size << ", " << tag << ")=" << ret << " >> End");
    return ret;
}

ssize_t lfi_recv(int id, void *data, size_t size) { return lfi_trecv(id, data, size, 0); }

ssize_t lfi_trecv(int id, void *data, size_t size, int tag) {
    ssize_t ret = -1;
    LFI::lfi_msg msg;
    debug_info("(" << id << ", " << data << ", " << size << ", " << tag << ")>> Begin");
    LFI::LFI &lfi = LFI::LFI::get_instance();
    msg = lfi.recv(id, data, size, tag);
    if (msg.error < 0) {
        ret = msg.error;
    } else {
        ret = msg.size;
    }
    debug_info("(" << id << ", " << data << ", " << size << ", " << tag << ")=" << ret << " >> End");
    return ret;
}

ssize_t lfi_any_recv(void *data1, void *data2, size_t size, int *out_source1, int *out_source2) {
    return lfi_any_trecv(data1, data2, size, 0, out_source1, out_source2);
}

ssize_t lfi_any_trecv(void *data1, void *data2, size_t size, int tag, int *out_source1, int *out_source2) {
    ssize_t ret = -1;
    debug_info("(" << data1 << ", " << data2 << ", " << size << ", " << tag << ")>> Begin");

    LFI::LFI &lfi = LFI::LFI::get_instance();
    auto [shm_msg, peer_msg] = lfi.any_recv(data1, data2, size, tag);

    if (shm_msg.error < 0 && peer_msg.error < 0) {
        ret = shm_msg.error;
        if (out_source1 != NULL) {
            (*out_source1) = -1;
        }
        if (out_source2 != NULL) {
            (*out_source2) = -1;
        }
    } else if (shm_msg.error >= 0 && peer_msg.error < 0) {
        ret = shm_msg.size;
        if (out_source1 != NULL) {
            (*out_source1) = shm_msg.source;
        }
        if (out_source2 != NULL) {
            (*out_source2) = -1;
        }
    } else if (shm_msg.error < 0 && peer_msg.error >= 0) {
        ret = peer_msg.size;
        if (out_source1 != NULL) {
            (*out_source1) = -1;
        }
        if (out_source2 != NULL) {
            (*out_source2) = peer_msg.source;
        }
    } else if (shm_msg.error >= 0 && peer_msg.error >= 0) {
        ret = shm_msg.size;
        if (out_source1 != NULL) {
            (*out_source1) = shm_msg.source;
        }
        if (out_source2 != NULL) {
            (*out_source2) = peer_msg.source;
        }
    }

    debug_info("(" << data1 << ", " << data2 << ", " << size << ", " << tag << ")= ret " << ret << " id1 "
                   << (out_source1 ? *out_source1 : -1) << " id2 " << (out_source2 ? *out_source2 : -1) << " >> End");
    return ret;
}

int lfi_server_close(int id) {
    int ret = -1;
    debug_info("(" << id << ") >> Begin");

    ret = LFI::socket::close(id);

    debug_info("(" << id << ")=" << ret << " >> End");
    return ret;
}

int lfi_client_close(int id) {
    int ret = -1;
    debug_info("(" << id << ") >> Begin");

    LFI::LFI &lfi = LFI::LFI::get_instance();
    ret = lfi.close_comm(id);

    debug_info("(" << id << ")=" << ret << " >> End");
    return ret;
}

const char *lfi_strerror(int error) { return LFI::lfi_strerror(std::abs(error)); }

#ifdef __cplusplus
}
#endif