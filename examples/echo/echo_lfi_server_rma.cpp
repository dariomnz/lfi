
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
// #define DEBUG

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

#include "echo_common.hpp"
#include "impl/debug.hpp"
#include "impl/ns.hpp"
#include "lfi.h"
#include "lfi_async.h"

using namespace bw_examples;

#define MAX_MSG_SIZE 4 * 1024 * 1024  // 4 Mb
#define TAG_MSG 100

static std::atomic<int> clients = 0;

std::vector<uint8_t> rma_data(MAX_MSG_SIZE);
lfi_mr_key rma_key{};

int main() {
    int new_socket;
    int server_fd;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    // Creating socket file descriptor
    int port = PORT_LFI;
    if ((server_fd = lfi_server_create(NULL, &port)) < 0) {
        perror("lfi server failed");
        exit(EXIT_FAILURE);
    }


    print("Server start accepting " << LFI::ns::get_host_name() << " :");
    while (true) {
        if ((new_socket = lfi_server_accept(server_fd)) < 0) {
            perror("accept");
            break;
        }
        rma_key = lfi_mr_reg(rma_data.data(), rma_data.size());
        if (rma_key.shm_key < 0) {
            perror("lfi_mr_reg failed");
            exit(EXIT_FAILURE);
        }
        uint64_t addr = reinterpret_cast<uint64_t>(rma_data.data()); 
        auto ret = lfi_send(new_socket, &addr, sizeof(addr));
        if (ret != sizeof(addr)) {
            perror("lfi_send addr");
            break;
        }
        ret = lfi_send(new_socket, &rma_key, sizeof(rma_key));
        if (ret != sizeof(rma_key)) {
            perror("lfi_send key");
            break;
        }

        print("Server accept client " << new_socket);
        clients++;
    }

    // closing the listening socket
    lfi_server_close(server_fd);

    return 0;
}
