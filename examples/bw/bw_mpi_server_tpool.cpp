
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
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>

#include "bw_common.hpp"
#include "impl/debug.hpp"
#include "impl/ns.hpp"
#include "impl/socket.hpp"
#include "mpi.h"
#include "thread_pool.hpp"

using namespace bw_examples;

static std::vector<uint8_t> data;

int run_test(MPI_Comm &client_comm, int rank, bw_test &test, ThreadPool &tpool) {
    ssize_t test_size = test.test_size;
    debug_info("Start run_test size " << test.test_size);
    std::vector<std::future<int>> v_futs;
    v_futs.reserve(test.test_count);
    v_futs.emplace_back(tpool.enqueue([rank, test_size, &client_comm, &test]() {
        for (size_t i = 0; i < test.test_count; i++) {
            int ret = 0;
            debug_info("count " << i << " MPI_Recv(data.data(), " << test_size << ")");
            ret = MPI_Recv(data.data(), test_size, MPI_UINT8_T, rank, 0, client_comm, MPI_STATUS_IGNORE);
            if (ret != MPI_SUCCESS) {
                printf("Error MPI_Recv\n");
                return -1;
            }

            // simulate work
            volatile int acum = 0;
            for (size_t w = 0; w < 1'000'000; w++) {
                acum += w;
            }

            int ack = 0;
            debug_info("ack MPI_Send(ack, " << sizeof(ack) << ")");
            ret = MPI_Send(&ack, 1, MPI_INT, rank, 0, client_comm);
            if (ret != MPI_SUCCESS) {
                printf("Error MPI_Recv\n");
                return -1;
            }
        }
        return 0;
    }));

    debug_info("v_futs size " << v_futs.size());

    for (auto &&fut : v_futs) {
        if (fut.valid()) {
            fut.get();
        }
    }

    debug_info("End run_test size " << test.test_size);

    return 0;
}

int main(int argc, char *argv[]) {
    int ret;
    int rank;
    const int max_clients = 1024;
    int server_fd = -1, new_socket;
    struct sockaddr_in address = {};
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    int provided;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    ret = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (ret < 0) exit(EXIT_FAILURE);

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (ret < 0) exit(EXIT_FAILURE);

    std::string port_name;
    port_name.resize(MPI_MAX_PORT_NAME);
    if (rank == 0) {
        ret = MPI_Open_port(MPI_INFO_NULL, port_name.data());

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
        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
            perror("bind failed");
            exit(EXIT_FAILURE);
        }
        if (listen(server_fd, 3) < 0) {
            perror("listen");
            exit(EXIT_FAILURE);
        }
    }

    auto &tests = get_test_vector();
    data.resize(tests[tests.size() - 1].test_size);

    ThreadPool tpool{};

    print("Server start accepting " << LFI::ns::get_host_name() << " :");
    int iter = 0;
    while (iter < max_clients) {
        if (rank == 0) {
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }
            LFI::socket::send(new_socket, port_name.data(), MPI_MAX_PORT_NAME);
            close(new_socket);
        }

        MPI_Comm client_comm;
        int ret = MPI_Comm_accept(port_name.c_str(), MPI_INFO_NULL, 0, MPI_COMM_WORLD, &client_comm);
        if (ret < 0) {
            printf("Error MPI_Comm_accept\n");
        }
        int client_size = 0;
        MPI_Comm_remote_size(client_comm, &client_size);
        print("Server accept client of size " << client_size);
        // printf("Client size: %d\n", client_size);
        std::vector<std::thread> v_threads;
        v_threads.reserve(client_size);
        for (int i = 0; i < client_size; i++) {
            v_threads.emplace_back(std::thread([&client_comm, i, &tests, &tpool]() {
                for (auto &test : tests) {
                    run_test(client_comm, i, test, tpool);
                }
            }));
        }
        for (auto &&t : v_threads) {
            t.join();
        }

        MPI_Comm_disconnect(&client_comm);

        iter++;
    }
    // closing the listening socket
    close(server_fd);

    ret = MPI_Close_port(port_name.data());
    if (ret < 0) exit(EXIT_FAILURE);

    ret = MPI_Finalize();
    if (ret < 0) exit(EXIT_FAILURE);

    return 0;
}
