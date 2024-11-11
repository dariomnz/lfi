
/*
 *  Copyright 2020-2024 Felix Garcia Carballeira, Diego Camarmas Alonso, Alejandro Calderon Mateos, Dario Muñoz Muñoz
 *
 *  This file is part of Expand.
 *
 *  Expand is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Expand is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Expand.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define DEBUG
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
    int ret = -1;
    debug_info("("<<serv_addr<<", "<<port<<")>> Begin");
    ret = socket::client_init(serv_addr, port);
    debug_info("("<<serv_addr<<", "<<port<<")="<<ret<<" >> End");
    return ret;
}

int lfi_server_accept(int socket)
{
    int ret = -1;
    debug_info("("<<socket<<") >> Begin");

    ret = socket::accept(socket);

    debug_info("("<<socket<<")="<<ret<<" >> End");
    return ret;
}

ssize_t lfi_send(int socket, const void *data, size_t size)
{
    ssize_t ret = -1;
    debug_info("("<<socket<<", "<<data<<", "<<size<<")>> Begin");
    ret = socket::send(socket, data, size);
    debug_info("("<<socket<<", "<<data<<", "<<size<<")="<<ret<<" >> End");
    return ret;
}

ssize_t lfi_recv(int socket, void *data, size_t size)
{
    ssize_t ret = -1;
    debug_info("("<<socket<<", "<<data<<", "<<size<<")>> Begin");
    ret = socket::recv(socket, data, size);
    debug_info("("<<socket<<", "<<data<<", "<<size<<")="<<ret<<" >> End");
    return ret;
}

int lfi_close(int socket)
{
    int ret = -1;
    debug_info("("<<socket<<") >> Begin");

    ret = socket::close(socket);

    debug_info("("<<socket<<")="<<ret<<" >> End");
    return ret;
}

#ifdef __cplusplus
}
#endif
    
} // namespace LFI