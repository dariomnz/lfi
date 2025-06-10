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

#define DEBUG
#include <aio.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <chrono>
#include <coroutine>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#include "coroutines.hpp"
#include "impl/debug.hpp"
#include "lfi.h"
#include "lfi_async.h"
#include "lfi_error.h"

static task<int> handleClient(int clientID) {
    std::vector<char> buffer(256, '\0');
    lfi_request* req = lfi_request_create(clientID);
    ssize_t ret = 0;
    while (true) {
        ret = co_await LFIRecvAwaitable(req, buffer.data(), buffer.size());
        if (ret < 0) {
            std::cerr << "Error in lfi_recv_async " << ret << " " << lfi_strerror(ret);
        }
        std::cout << "Received: " << std::string_view(buffer.data(), ret) << std::endl;

        std::string file_path(buffer.data(), ret);
        int fd = open(file_path.c_str(), O_RDONLY);
        if (fd < 0) {
            print("Error opening file " << file_path << " " << strerror(-fd));
            ssize_t err = fd;
            ret = co_await LFISendAwaitable(req, &err, sizeof(err));
            if (ret < 0) {
                std::cerr << "Error in lfi_send_async " << ret << " " << lfi_strerror(ret);
            }
            print_error("Send error");
            continue;
        }

        ssize_t readed = 0;
        ssize_t total_readed = 0;
        while ((readed = co_await ReadAwaitable(fd, buffer.data(), buffer.size(), total_readed)) > 0) {
            print("readed "<<readed);
            ret = co_await LFISendAwaitable(req, &readed, sizeof(readed));
            if (ret < 0) {
                std::cerr << "Error in lfi_send_async " << ret << " " << lfi_strerror(ret);
            }
            ret = co_await LFISendAwaitable(req, buffer.data(), readed);
            if (ret < 0) {
                std::cerr << "Error in lfi_send_async " << ret << " " << lfi_strerror(ret);
            }
            total_readed += readed;
        }
        if (readed < 0) {
            print_error("Error reading " << readed << " " << strerror(-readed));
        }
        // Send 0 to finish
        ret = co_await LFISendAwaitable(req, &readed, sizeof(readed));
        if (ret < 0) {
            std::cerr << "Error in lfi_send_async " << ret << " " << lfi_strerror(ret);
        }
        ret = close(fd);
        if (ret < 0) {
            print_error("Error closing file " << file_path);
        }
    }

    lfi_client_close(clientID);
    std::cout << "Client disconected" << std::endl;
    co_return 0;
}
static task<int> acceptConnections(int serverSocket) {
    while (true) {
        int clientSocket = co_await LFIAcceptAwaitable(serverSocket);
        if (clientSocket != -1) {
            std::cout << "Cliente conectado\n";
            handleClient(clientSocket);
        }
    }
}

int main() {
    int port = 8080;
    int server_id = lfi_server_create(nullptr, &port);
    if (server_id == -1) {
        perror("Error al crear el socket");
        return 1;
    }

    std::cout << "Servidor escuchando en el puerto " << port << "..." << std::endl;

    progress_loop& loop = progress_loop::get_instance();

    auto server_task = acceptConnections(server_id);

    while (!server_task.done()) {
        loop.run_one_step();
    }

    lfi_server_close(server_id);
    return 0;
}
