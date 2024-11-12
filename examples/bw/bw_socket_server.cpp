
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

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include "mpi.h"
#include "bw_common.hpp"
#include "impl/socket.hpp"

using namespace bw_examples;

int run_test(int socket, bw_test &test)
{
    std::vector<uint8_t> data(test.test_size);
    ssize_t data_send = 0;
    ssize_t data_recv = 0;
    ssize_t test_size = test.test_size;
    timer t;
    for (size_t i = 0; i < test.test_count; i++)
    {
        data_recv = LFI::socket::recv(socket, data.data(), test_size);
        if (data_recv != test_size)
            return -1;
        test.recv_microsec += t.resetElapsedMicro();
        test.recv_size += data_recv;

        data_send = LFI::socket::send(socket, data.data(), test_size);
        if (data_send != test_size)
            return -1;
        test.send_microsec += t.resetElapsedMicro();
        test.send_size += data_send;
    }

    return 0;
}

int main(int argc, char *argv[])
{   
    int ret;
    const int max_clients = 1000;
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    ret = MPI_Init(&argc, &argv);
    if (ret < 0) exit(EXIT_FAILURE);

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Forcefully attaching socket to the port 8080
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    auto &tests = get_test_vector();

    int iter = 0;
    while (iter < max_clients)
    {
        if ((new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        std::thread([new_socket, &tests](){
           
            for (auto &test : tests)
            {
                run_test(new_socket, test);
            }
            close(new_socket);
        }).detach();
        iter++;
    }
    // closing the listening socket
    close(server_fd);

    ret = MPI_Finalize();
    if (ret < 0) exit(EXIT_FAILURE);

    return 0;
}
