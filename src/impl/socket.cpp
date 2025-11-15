
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

#include <poll.h>
#include <unistd.h>

#include "impl/debug.hpp"
#include "impl/ns.hpp"
#include "impl/profiler.hpp"

namespace LFI {

int socket::server_init(const std::string& addr, int& port) {
    LFI_PROFILE_FUNCTION();
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
        debug_info("Socket bind to " << addr);
        if (::inet_pton(AF_INET, addr.c_str(), &server_addr.sin_addr) <= 0) {
            print_error("Error: Invalid IP address or conversion error in addr '" << addr << "'");
            return -1;
        }
    } else {
        debug_info("Socket bind to INADDR_ANY");
        server_addr.sin_addr.s_addr = INADDR_ANY;
    }
    server_addr.sin_port = ::htons(port);

    ret = ::bind(socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        print_error("ERROR: bind fails");
        socket::close(socket);
        return ret;
    }

    // listen
    debug_info("Socket listen");

    ret = ::listen(socket, 1024);
    if (ret < 0) {
        print_error("ERROR: listen fails");
        socket::close(socket);
        return ret;
    }
    socklen_t len = sizeof(server_addr);
    ::getsockname(socket, (struct sockaddr*)&server_addr, &len);
    port = ::ntohs(server_addr.sin_port);

    debug_info("available at " << port);

    debug_info(">> End = " << socket);
    return socket;
}

int socket::retry_connect(int socket, sockaddr* addr, socklen_t len, int timeout_ms, int time_to_sleep_ms) {
    LFI_PROFILE_FUNCTION();
    int ret = -1;
    auto start = std::chrono::high_resolution_clock::now();
    while (ret < 0) {
        debug_info("Try to connect server");
        ret = ::connect(socket, addr, len);
        if (ret < 0) {
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start)
                    .count();
            debug_error("Failed to connect. Elapsed time " << elapsed << " ms");
            if (elapsed > timeout_ms) {
                debug_error("Socket connection");
                return ret;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(time_to_sleep_ms));
        }
    }
    return ret;
}

int socket::client_init(const std::string& addr, int port, int timeout_ms, bool is_ip) {
    LFI_PROFILE_FUNCTION();
    int ret = -1;
    int socket = -1;
    debug_info(">> Begin");

    if (is_ip == false) {
        struct addrinfo hints = {};
        struct addrinfo* res;

        hints.ai_family = AF_INET;        // Allow IPv4 or IPv6
        hints.ai_socktype = SOCK_STREAM;  // TCP socket

        // Get address information
        debug_info("Before getaddrinfo");
        int status = getaddrinfo(addr.c_str(), std::to_string(port).c_str(), &hints, &res);
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
            debug_info("Attempt connect to " << ns::sockaddr_to_str(p->ai_addr) << " on port " << port);
            if ((ret = retry_connect(socket, p->ai_addr, p->ai_addrlen, timeout_ms, timeout_ms / 10)) == -1) {
                print_error("connect " << addr << " port " << port);
                socket::close(socket);
                continue;
            }

            // Successfully connected
            debug_info("Connected to " << addr << " on port " << port);
            break;
        }

        // Free the linked list created by getaddrinfo
        ::freeaddrinfo(res);
    } else {
        struct sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);

        debug_info("Socket bind to " << addr);
        if (::inet_pton(AF_INET, addr.c_str(), &server_addr.sin_addr) <= 0) {
            print_error("Error: Invalid IP address or conversion error in addr '" << addr << "'");
            return -1;
        }
        socket = socket::open();
        if (socket < 0) {
            return socket;
        }
        if ((ret = ::connect(socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr))) == -1) {
            print_error("connect " << addr << " port " << port);
            socket::close(socket);
            return -1;
        }
    }

    // If no valid connection was made
    if (ret == -1) {
        print("Failed to connect");
        return -1;
    }

    int buf = 123;
    ret = send(socket, &buf, sizeof(buf));
    if (ret != sizeof(buf)) {
        return -1;
    }
    ret = recv(socket, &buf, sizeof(buf));
    if (ret != sizeof(buf)) {
        return -1;
    }

    debug_info(">> End = " << socket);
    return socket;
}

int socket::open() {
    LFI_PROFILE_FUNCTION();
    int ret, val;
    int out_socket = -1;

    debug_info(">> Begin");

    // Socket init
    debug_info("Socket init");

    out_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (out_socket < 0) {
        print_error("ERROR: socket fails");
        return out_socket;
    }

    // tcp_nodelay
    debug_info("TCP nodelay");

    val = 1;
    ret = ::setsockopt(out_socket, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    if (ret < 0) {
        print_error("ERROR: setsockopt fails");
        socket::close(out_socket);
        return ret;
    }

    // sock_reuseaddr
    debug_info("Socket reuseaddr");

    val = 1;
    ret = ::setsockopt(out_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&val, sizeof(int));
    if (ret < 0) {
        print_error("ERROR: setsockopt fails");
        socket::close(out_socket);
        return ret;
    }

    debug_info(">> End = " << out_socket);
    return out_socket;
}

int socket::accept_timeout(int socket, int timeout_ms) {
    LFI_PROFILE_FUNCTION();
    sockaddr_in client_addr;
    socklen_t size = sizeof(sockaddr_in);
    if (timeout_ms == 0) {
        return ::accept(socket, (struct sockaddr*)&client_addr, &size);
    }
    struct pollfd fds[1];
    fds[0].fd = socket;
    fds[0].events = POLLIN;
    int current_timeout_ms = timeout_ms;
    auto start = std::chrono::high_resolution_clock::now();
    while (current_timeout_ms > 0) {
        int ret = poll(fds, 1, current_timeout_ms);

        if (ret < 0) {
            if (errno == EINTR) {
                // Interrumped for signal
                current_timeout_ms -= std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::high_resolution_clock::now() - start)
                                          .count();
                if (current_timeout_ms <= 0) return -1;
                continue;
            }
            return -1;
        } else if (ret == 0) {
            // Timeout
            return -1;
        } else {
            if (fds[0].revents & POLLIN) {
                return ::accept(socket, (struct sockaddr*)&client_addr, &size);
            } else {
                return -1;
            }
        }
    }
    return -1;
}

int socket::accept(int socket, int timeout_ms) {
    LFI_PROFILE_FUNCTION();
    int ret, flag, new_socket;
    // Accept
    debug_info(">> Begin");

    new_socket = accept_timeout(socket, timeout_ms);
    if (new_socket < 0) {
        print_error("ERROR: accept fails");
        return -1;
    }

    // tcp_nodelay
    flag = 1;
    ret = ::setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    if (ret < 0) {
        print_error("setsockopt: ");
        socket::close(new_socket);
        return -1;
    }

    int buf = 123;
    ret = recv(new_socket, &buf, sizeof(buf));
    if (ret != sizeof(buf)) {
        return -1;
    }
    ret = send(new_socket, &buf, sizeof(buf));
    if (ret != sizeof(buf)) {
        return -1;
    }

    debug_info(">> End = " << new_socket);
    return new_socket;
}

int socket::close(int socket) {
    LFI_PROFILE_FUNCTION();
    int ret = -1;
    debug_info(">> Begin");
    ret = ::close(socket);
    debug_info(">> End = " << ret);
    return ret;
}

ssize_t socket::send(int socket, const void* data, size_t len) {
    LFI_PROFILE_FUNCTION();
    int r;
    int l = len;
    const char* buffer = static_cast<const char*>(data);
    debug_info(">> Begin");

    do {
        r = ::send(socket, buffer, l, 0);
        if (r < 0) return r; /* fail */

        l = l - r;
        buffer = buffer + r;

    } while ((l > 0) && (r >= 0));

    debug_info(">> End = " << len);
    return len;
}

ssize_t socket::recv(int socket, void* data, size_t len) {
    LFI_PROFILE_FUNCTION();
    int r;
    int l = len;
    debug_info(">> Begin");
    char* buffer = static_cast<char*>(data);

    do {
        r = ::recv(socket, buffer, l, 0);
        if (r < 0) return r; /* fail */

        l = l - r;
        buffer = buffer + r;

    } while ((l > 0) && (r >= 0));

    debug_info(">> End = " << len);
    return len;
}

int64_t socket::send_str(int socket, const std::string& str) {
    LFI_PROFILE_FUNCTION();
    int64_t ret;
    size_t size_str = str.size();
    debug_info("Send_str size " << size_str);

    do {
        ret = socket::send(socket, &size_str, sizeof(size_str));
    } while (ret < 0 && errno == EAGAIN);
    if (ret != sizeof(size_str)) {
        print_error("send size of string");
        return ret;
    }
    if (size_str == 0) {
        return size_str;
    }
    debug_info("Send_str " << str);
    do {
        ret = socket::send(socket, &str[0], size_str);
    } while (ret < 0 && errno == EAGAIN);
    if (ret != static_cast<int64_t>(size_str)) {
        print_error("send string");
        return ret;
    }
    return ret;
}

int64_t socket::recv_str(int socket, std::string& str) {
    LFI_PROFILE_FUNCTION();
    int64_t ret;
    size_t size_str = 0;
    do {
        ret = socket::recv(socket, &size_str, sizeof(size_str));
    } while (ret < 0 && errno == EAGAIN);
    if (ret != sizeof(size_str)) {
        print_error("send size of string");
        return ret;
    }
    debug_info("Recv_str size " << size_str);
    if (size_str == 0) {
        return size_str;
    }
    str.clear();
    str.resize(size_str, '\0');
    do {
        ret = socket::recv(socket, &str[0], size_str);
    } while (ret < 0 && errno == EAGAIN);
    if (ret != static_cast<int64_t>(size_str)) {
        print_error("send string");
        return ret;
    }
    debug_info("Recv_str " << str);
    return ret;
}
}  // namespace LFI