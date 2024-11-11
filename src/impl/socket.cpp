
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
#include "impl/socket.hpp"

#include <unistd.h>

#include "impl/debug.hpp"

namespace LFI {
int socket::server_init(const std::string& addr, int& port) {
    struct sockaddr_in server_addr = {};
    int ret;

    debug_info(">> Begin");
    int socket = socket::open();
    if (socket < 0) {
        print("Error opening a socket in addr " << addr << " port " << port);
        return socket;
    }

    // bind
    debug_info("Socket bind");

    server_addr.sin_family = AF_INET;
    if (!addr.empty()) {
        debug_info("Socket bind to "<<addr);
        if (::inet_pton(AF_INET, addr.c_str(), &server_addr.sin_addr) <= 0) {
            print("Error: Invalid IP address or conversion error in addr '" << addr << "'");
            return -1;
        }
    } else {
        debug_info("Socket bind to INADDR_ANY");
        server_addr.sin_addr.s_addr = INADDR_ANY;
    }
    server_addr.sin_port = ::htons(port);

    ret = ::bind(socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        print("ERROR: bind fails");
        socket::close(socket);
        return ret;
    }

    // listen
    debug_info("Socket listen");

    ret = ::listen(socket, 20);
    if (ret < 0) {
        print("ERROR: listen fails");
        socket::close(socket);
        return ret;
    }
    socklen_t len = sizeof(server_addr);
    ::getsockname(socket, (struct sockaddr*)&server_addr, &len);
    port = ntohs(server_addr.sin_port);

    debug_info("available at " << port);

    debug_info(">> End");
    return socket;
}

int socket::client_init(const std::string& addr, int port) {
    int ret;
    int socket;
    debug_info(">> Begin");

    struct addrinfo hints = {};
    struct addrinfo* res;

    hints.ai_family = AF_UNSPEC;      // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP socket

    // Get address information
    int status = getaddrinfo(addr.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (status != 0) {
        print("getaddrinfo error: " << gai_strerror(status));
        return -1;
    }

    // Try to connect to one of the results returned by getaddrinfo
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        socket = socket::open();
        if (socket < 0) {
            return socket;
        }
        // Attempt to connect
        if ((ret = connect(socket, p->ai_addr, p->ai_addrlen)) == -1) {
            perror("connect");
            socket::close(socket);
            continue;
        }

        // Successfully connected
        debug_info("Connected to " << addr << " on port " << port);
        break;
    }

    // Free the linked list created by getaddrinfo
    freeaddrinfo(res);

    // If no valid connection was made
    if (ret == -1) {
        print("Failed to connect");
        return -1;
    }

    debug_info(">> End");
    return socket;
}

int socket::open() {
    int ret, val;
    int out_socket = -1;

    debug_info(">> Begin");

    // Socket init
    debug_info("Socket init");

    out_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (out_socket < 0) {
        print("ERROR: socket fails");
        return out_socket;
    }

    // tcp_nodalay
    debug_info("TCP nodelay");

    val = 1;
    ret = ::setsockopt(out_socket, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    if (ret < 0) {
        print("ERROR: setsockopt fails");
        socket::close(out_socket);
        return ret;
    }

    // sock_reuseaddr
    debug_info("Socket reuseaddr");

    val = 1;
    ret = ::setsockopt(out_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&val, sizeof(int));
    if (ret < 0) {
        print("ERROR: setsockopt fails");
        socket::close(out_socket);
        return ret;
    }

    debug_info(">> End");
    return out_socket;
}

int socket::accept(int socket) {
    int ret, flag, new_socket;
    sockaddr_in client_addr;
    socklen_t size = sizeof(sockaddr_in);
    // Accept
    debug_info(">> Begin");

    new_socket = ::accept(socket, (sockaddr*)&client_addr, &size);
    if (new_socket < 0) {
        print("ERROR: accept fails");
        return -1;
    }

    // tcp_nodelay
    flag = 1;
    ret = ::setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    if (ret < 0) {
        perror("setsockopt: ");
        socket::close(new_socket);
        return -1;
    }

    debug_info(">> End");
    return new_socket;
}

int socket::close(int socket) {
    int ret = -1;
    debug_info(">> Begin");
    ret = ::close(socket);
    debug_info(">> End");
    return ret;
}

ssize_t socket::send(int socket, const void* data, size_t len) {
    int r;
    int l = len;
    const char* buffer = static_cast<const char*>(data);
    debug_info(">> Begin");

    do {
        r = write(socket, buffer, l);
        if (r < 0) return r; /* fail */

        l = l - r;
        buffer = buffer + r;

    } while ((l > 0) && (r >= 0));

    debug_info(">> End");
    return len;
}

ssize_t socket::recv(int socket, void* data, size_t len) {
    int r;
    int l = len;
    debug_info(">> Begin");
    char* buffer = static_cast<char*>(data);

    do {
        r = read(socket, buffer, l);
        if (r < 0) return r; /* fail */

        l = l - r;
        buffer = buffer + r;

    } while ((l > 0) && (r >= 0));

    debug_info(">> End");
    return len;
}
}  // namespace LFI