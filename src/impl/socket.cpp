
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

#include "impl/socket.hpp"

#include <unistd.h>

#include "impl/debug.hpp"
#include "impl/proxy.hpp"

namespace LFI {
int socket::server_init(const std::string& addr, int& port) {
    struct sockaddr_in server_addr = {};
    int ret;

    debug_info(">> Begin");
    int socket = socket::open();
    if (socket < 0) {
        print_error("Error opening a socket in addr " << addr << " port " << port);
        return socket;
    }

    // bind
    debug_info("Socket bind");

    server_addr.sin_family = AF_INET;
    if (!addr.empty()) {
        debug_info("Socket bind to "<<addr);
        if (PROXY(inet_pton)(AF_INET, addr.c_str(), &server_addr.sin_addr) <= 0) {
            print_error("Error: Invalid IP address or conversion error in addr '" << addr << "'");
            return -1;
        }
    } else {
        debug_info("Socket bind to INADDR_ANY");
        server_addr.sin_addr.s_addr = INADDR_ANY;
    }
    server_addr.sin_port = PROXY(htons)(port);

    ret = PROXY(bind)(socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        print_error("ERROR: bind fails");
        socket::close(socket);
        return ret;
    }

    // listen
    debug_info("Socket listen");

    ret = PROXY(listen)(socket, 20);
    if (ret < 0) {
        print_error("ERROR: listen fails");
        socket::close(socket);
        return ret;
    }
    socklen_t len = sizeof(server_addr);
    PROXY(getsockname)(socket, (struct sockaddr*)&server_addr, &len);
    port = PROXY(ntohs)(server_addr.sin_port);

    debug_info("available at " << port);

    debug_info(">> End = "<<socket);
    return socket;
}

int socket::client_init(const std::string& addr, int port) {
    int ret = -1;
    int socket;
    debug_info(">> Begin");

    struct addrinfo hints = {};
    struct addrinfo* res;

    hints.ai_family = AF_INET;      // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP socket

    // Get address information
    int status = PROXY(getaddrinfo)(addr.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (status != 0) {
        print_error("getaddrinfo error: " << gai_strerror(status));
        return -1;
    }

    // Try to connect to one of the results returned by getaddrinfo
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        socket = socket::open();
        if (socket < 0) {
            return socket;
        }
        // Attempt to connect
        if ((ret = PROXY(connect)(socket, p->ai_addr, p->ai_addrlen)) == -1) {
            print_error("connect "<<addr<<" port "<<port);
            socket::close(socket);
            continue;
        }

        // Successfully connected
        debug_info("Connected to " << addr << " on port " << port);
        break;
    }

    // Free the linked list created by getaddrinfo
    PROXY(freeaddrinfo)(res);

    // If no valid connection was made
    if (ret == -1) {
        print("Failed to connect");
        return -1;
    }

    int buf = 123;
    ret = send(socket, &buf, sizeof(buf));
    if (ret != sizeof(buf)){
        return -1;
    }
    ret = recv(socket, &buf, sizeof(buf));
    if (ret != sizeof(buf)){
        return -1;
    }

    debug_info(">> End = "<<socket);
    return socket;
}

int socket::open() {
    int ret, val;
    int out_socket = -1;

    debug_info(">> Begin");

    // Socket init
    debug_info("Socket init");

    out_socket = PROXY(socket)(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (out_socket < 0) {
        print_error("ERROR: socket fails");
        return out_socket;
    }

    // tcp_nodelay
    debug_info("TCP nodelay");

    val = 1;
    ret = PROXY(setsockopt)(out_socket, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    if (ret < 0) {
        print_error("ERROR: setsockopt fails");
        socket::close(out_socket);
        return ret;
    }

    // sock_reuseaddr
    debug_info("Socket reuseaddr");

    val = 1;
    ret = PROXY(setsockopt)(out_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&val, sizeof(int));
    if (ret < 0) {
        print_error("ERROR: setsockopt fails");
        socket::close(out_socket);
        return ret;
    }

    debug_info(">> End = "<<out_socket);
    return out_socket;
}

int socket::accept(int socket) {
    int ret, flag, new_socket;
    sockaddr_in client_addr;
    socklen_t size = sizeof(sockaddr_in);
    // Accept
    debug_info(">> Begin");

    new_socket = PROXY(accept)(socket, (sockaddr*)&client_addr, &size);
    if (new_socket < 0) {
        print_error("ERROR: accept fails");
        return -1;
    }

    // tcp_nodelay
    flag = 1;
    ret = PROXY(setsockopt)(new_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    if (ret < 0) {
        print_error("setsockopt: ");
        socket::close(new_socket);
        return -1;
    }

    int buf = 123;
    ret = recv(new_socket, &buf, sizeof(buf));
    if (ret != sizeof(buf)){
        return -1;
    }
    ret = send(new_socket, &buf, sizeof(buf));
    if (ret != sizeof(buf)){
        return -1;
    }

    debug_info(">> End = "<<new_socket);
    return new_socket;
}

int socket::close(int socket) {
    int ret = -1;
    debug_info(">> Begin");
    ret = PROXY(close)(socket);
    debug_info(">> End = "<<ret);
    return ret;
}

ssize_t socket::send(int socket, const void* data, size_t len) {
    int r;
    int l = len;
    const char* buffer = static_cast<const char*>(data);
    debug_info(">> Begin");

    do {
        r = PROXY(send)(socket, buffer, l, 0);
        if (r < 0) return r; /* fail */

        l = l - r;
        buffer = buffer + r;

    } while ((l > 0) && (r >= 0));

    debug_info(">> End = "<<len);
    return len;
}

ssize_t socket::recv(int socket, void* data, size_t len) {
    int r;
    int l = len;
    debug_info(">> Begin");
    char* buffer = static_cast<char*>(data);

    do {
        r = PROXY(recv)(socket, buffer, l, 0);
        if (r < 0) return r; /* fail */

        l = l - r;
        buffer = buffer + r;

    } while ((l > 0) && (r >= 0));

    debug_info(">> End = "<<len);
    return len;
}
}  // namespace LFI