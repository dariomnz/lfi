
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

int lfi_client_create_t(const char *serv_addr, int port, int timeout_ms) {
    int out = -1;
    int client_socket;
    debug_info("(" << serv_addr << ", " << port << ")>> Begin");

    client_socket = LFI::socket::client_init(serv_addr, port, timeout_ms);
    if (client_socket < 0) {
        print_error("socket::client_init (" << serv_addr << ", " << port << ")");
        return client_socket;
    }
    LFI::LFI &lfi = LFI::LFI::get_instance();

    out = lfi.reserve_comm();

    auto func = [client_socket, out, &lfi]() {
        uint32_t ret = lfi.init_client(client_socket, out);
        // TODO handle error in close
        LFI::socket::close(client_socket);
        return ret;
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

int lfi_client_create(const char *serv_addr, int port) {
    return lfi_client_create_t(serv_addr, port, 0);
}

int lfi_server_accept_t(int socket, int timeout_ms) {
    uint32_t out = -1;
    int client_socket;
    debug_info("(" << socket << ") >> Begin");

    client_socket = LFI::socket::accept(socket, timeout_ms);
    if (client_socket < 0) {
        print_error("socket::accept (" << socket << ", " << timeout_ms << ")");
        return client_socket;
    }
    LFI::LFI &lfi = LFI::LFI::get_instance();

    out = lfi.reserve_comm();
    auto func = [client_socket, out, &lfi]() {
        uint32_t ret = lfi.init_server(client_socket, out);
        // TODO handle error in close
        LFI::socket::close(client_socket);
        return ret;
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

int lfi_server_accept(int socket) {
    return lfi_server_accept_t(socket, 0);
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