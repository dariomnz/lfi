
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
#include "impl/fabric.hpp"
#include "impl/socket.hpp"
#include "impl/debug.hpp"

namespace LFI
{

#ifdef __cplusplus
extern "C" {
#endif

int lfi_server_create(char* serv_addr, int port)
{
    int ret = -1;
    std::string addr = "";
    if (serv_addr != nullptr)
        addr = serv_addr;
    debug_info("("<<addr<<", "<<port<<")>> Begin");
    ret = socket::server_init(addr, port);
    debug_info("("<<addr<<", "<<port<<")="<<ret<<" >> End");
    return ret;
}

int lfi_client_create(char* serv_addr, int port)
{
    int out = -1;
    int client_socket;
    debug_info("("<<serv_addr<<", "<<port<<")>> Begin");

    client_socket = socket::client_init(serv_addr, port);

    out = LFI::init_client(client_socket);

    // TODO handle error in close
    socket::close(client_socket);

    debug_info("("<<serv_addr<<", "<<port<<")="<<out<<" >> End");
    return out;
}

int lfi_server_accept(int socket)
{
    int out = -1;
    int client_socket;
    debug_info("("<<socket<<") >> Begin");

    client_socket = socket::accept(socket);

    out = LFI::init_server(client_socket);


    debug_info("("<<socket<<")="<<out<<" >> End");
    return out;
}

ssize_t lfi_send(int id, const void *data, size_t size)
{
    ssize_t ret = -1;
    fabric_msg msg;
    debug_info("("<<id<<", "<<data<<", "<<size<<")>> Begin");
    msg = LFI::send(id, data, size, 0);
    if (msg.error < 0){
        ret = msg.error;
    }else{
        ret = msg.size;
    }
    debug_info("("<<id<<", "<<data<<", "<<size<<")="<<ret<<" >> End");
    return ret;
}

ssize_t lfi_tsend(int id, const void *data, size_t size, int tag)
{
    ssize_t ret = -1;
    fabric_msg msg;
    debug_info("("<<id<<", "<<data<<", "<<size<<", "<<tag<<")>> Begin");
    msg = LFI::send(id, data, size, tag);
    if (msg.error < 0){
        ret = msg.error;
    }else{
        ret = msg.size;
    }
    debug_info("("<<id<<", "<<data<<", "<<size<<", "<<tag<<")="<<ret<<" >> End");
    return ret;
}

ssize_t lfi_recv(int id, void *data, size_t size)
{
    ssize_t ret = -1;
    fabric_msg msg;
    debug_info("("<<id<<", "<<data<<", "<<size<<")>> Begin");
    msg = LFI::recv(id, data, size, 0);
    if (msg.error < 0){
        ret = msg.error;
    }else{
        ret = msg.size;
    }
    debug_info("("<<id<<", "<<data<<", "<<size<<")="<<ret<<" >> End");
    return ret;
}

ssize_t lfi_trecv(int id, void *data, size_t size, int tag)
{
    ssize_t ret = -1;
    fabric_msg msg;
    debug_info("("<<id<<", "<<data<<", "<<size<<", "<<tag<<")>> Begin");
    msg = LFI::recv(id, data, size, tag);
    if (msg.error < 0){
        ret = msg.error;
    }else{
        ret = msg.size;
    }
    debug_info("("<<id<<", "<<data<<", "<<size<<", "<<tag<<")="<<ret<<" >> End");
    return ret;
}

int lfi_server_close(int id)
{
    int ret = -1;
    debug_info("("<<id<<") >> Begin");

    ret = socket::close(id);

    debug_info("("<<id<<")="<<ret<<" >> End");
    return ret;
}

int lfi_client_close(int id)
{
    int ret = -1;
    debug_info("("<<id<<") >> Begin");

    ret = LFI::close_comm(id);

    debug_info("("<<id<<")="<<ret<<" >> End");
    return ret;
}

#ifdef __cplusplus
}
#endif
    
} // namespace LFI