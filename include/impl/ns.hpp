
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
#include <impl/debug.hpp>

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

            for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
                // Attempt to connect
                debug_info("HOST: " << hostname << " IP: " << sockaddr_to_str(p->ai_addr));
            }

            // Convert the IP address to a string
            char ip_str[INET_ADDRSTRLEN];
            void* addr_ptr = &((struct sockaddr_in*) res->ai_addr)->sin_addr;
            inet_ntop(AF_INET, addr_ptr, ip_str, sizeof(ip_str));

            freeaddrinfo(res);

            return std::string(ip_str);
        }
    
        static std::string get_host_name_by_ip(const std::string& ip_address) {
            struct sockaddr_in sa;
            char host[NI_MAXHOST];

            sa.sin_family = AF_INET;
            if (inet_pton(AF_INET, ip_address.c_str(), &sa.sin_addr) != 1) {
                return "Invalid IP address";
            }

            int result = getnameinfo((struct sockaddr*)&sa, sizeof(sa), host, NI_MAXHOST, nullptr, 0, 0);
            if (result != 0) {
                return std::string("Error: ") + gai_strerror(result);
            }

            return std::string(host);
        }

        static std::string sockaddr_to_str(const struct sockaddr *addr)
        {
            if (addr == nullptr)
            {
                return "";
            }

            char buffer[INET6_ADDRSTRLEN] = {};
            switch (addr->sa_family)
            {
            case AF_INET:
            {
                // IPv4
                const struct sockaddr_in *addr_in = reinterpret_cast<const struct sockaddr_in *>(addr);
                inet_ntop(AF_INET, &(addr_in->sin_addr), buffer, sizeof(buffer));
                return buffer;
            }
            case AF_INET6:
            {
                // IPv6
                const struct sockaddr_in6 *addr_in6 = reinterpret_cast<const struct sockaddr_in6 *>(addr);
                inet_ntop(AF_INET6, &(addr_in6->sin6_addr), buffer, sizeof(buffer));
                return buffer;
            }
            }

            throw std::runtime_error("In sockaddr_to_str, addr->sa_family not AF_INET or AF_INET6");
            return "";
        }
	};
} // namespace LFI
