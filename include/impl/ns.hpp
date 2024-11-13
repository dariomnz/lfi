
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

#include <iostream>
#include <string>
#include <unistd.h>
#include <limits.h>
#include <netdb.h>
#include <arpa/inet.h>

namespace LFI
{
    class ns
	{
    public:
        static std::string get_host_name()
        {
            char hostname[HOST_NAME_MAX];
            if (gethostname(hostname, HOST_NAME_MAX) == 0){
                return std::string(hostname);
            }
            return "";
        }

        static std::string get_host_ip(std::string hostname = "")
        {
            if (hostname.empty()){
                hostname = get_host_name();
            }

            struct addrinfo hints{}, *res;
            hints.ai_family = AF_INET; // IPv4 only
            hints.ai_socktype = SOCK_STREAM;

            // Get the IP address using getaddrinfo
            if (getaddrinfo(hostname.c_str(), nullptr, &hints, &res) != 0) {
                std::cerr << "Error en getaddrinfo" << std::endl;
                return "";
            }

            // Convert the IP address to a string
            char ip_str[INET_ADDRSTRLEN];
            void* addr_ptr = &((struct sockaddr_in*) res->ai_addr)->sin_addr;
            inet_ntop(AF_INET, addr_ptr, ip_str, sizeof(ip_str));

            freeaddrinfo(res);

            return std::string(ip_str);
        }
	};
} // namespace LFI
