
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

#pragma once

#include <string>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

namespace LFI {

class socket {
private:
    static int open();
public:
    static int server_init(const std::string& addr, int& port);
    static int client_init(const std::string& addr, int port);
    static int accept(int socket);
    static int close(int socket);

    static ssize_t send(int socket, const void* data, size_t len);
    static ssize_t recv(int socket, void* data, size_t len);
};

}  // namespace LFI