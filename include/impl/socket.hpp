
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

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>

#include <string>

namespace LFI {

class socket {
   private:
    static int open();

   public:
    static int server_init(const std::string &addr, int &port);
    static int retry_connect(int socket, sockaddr *addr, socklen_t len, int timeout_ms, int time_to_sleep_ms);
    static int accept_timeout(int socket, int timeout_ms);
    static int client_init(const std::string &addr, int port, int timeout_ms = 0, bool is_ip = false);
    static int accept(int socket, int timeout_ms = 0);
    static int close(int socket);

    static ssize_t send(int socket, const void *data, size_t len);
    static ssize_t recv(int socket, void *data, size_t len);
    static int64_t send_str(int socket, const std::string &str);
    static int64_t recv_str(int socket, std::string &str);
};

}  // namespace LFI