
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
#include <unordered_set>

#include "echo_common.hpp"
#include "impl/debug.hpp"
#include "impl/ns.hpp"
#include "impl/socket.hpp"
#include "mpi.h"
#include "mpi_proto.h"
#include "thread_pool.hpp"

using namespace bw_examples;

std::mutex data_mutex;
std::vector<uint8_t> data;

#define TAG_MSG 100

void echo_server(MPI_Comm comm) {
    int msg_size = 0;
    std::unordered_set<std::unique_ptr<MPI_Request>> async_req;
    while (true) {
        debug_info("Start recv any ack");
        MPI_Status status;

        auto ret = MPI_Recv(&msg_size, 1, MPI_INT, MPI_ANY_SOURCE, TAG_MSG, comm, &status);
        if (ret != MPI_SUCCESS) {
            print("Error in wait_num");
            return;
        }

        if (msg_size == 0) {
            print("Server disconnect");
            MPI_Comm_disconnect(&comm);
            return;
        }

        auto msg_op = [msg_size, rank = status.MPI_SOURCE, &comm, &async_req]() {
            {
                std::unique_lock lock(data_mutex);
                if (std::abs(msg_size) > static_cast<int>(data.size())) {
                    data.resize(std::abs(msg_size));
                }
            }
            auto [req, b] = async_req.emplace(std::make_unique<MPI_Request>());
            if (msg_size < 0) {
                debug_info("MPI_Recv(" << rank << ", data.data(), " << std::abs(msg_size) << ")");
                auto recv_msg = MPI_Irecv(data.data(), std::abs(msg_size), MPI_UINT8_T, rank, 0, comm, req->get());
                if (recv_msg != MPI_SUCCESS) {
                    char msg[1024];
                    int msg_len = 1024;
                    MPI_Error_string(recv_msg, msg, &msg_len);
                    print("Error MPI_Recv = " << recv_msg << " " << msg);
                    return -1;
                }
                // int ack = 0;
                // debug_info("MPI_Send(" << rank << ", &ack, " << sizeof(ack) << ")");
                // auto send_ack = MPI_Send(&ack, 1, MPI_INT, rank, 0, comm);
                // if (send_ack != MPI_SUCCESS) {
                //     char msg[1024];
                //     int msg_len = 1024;
                //     MPI_Error_string(send_ack, msg, &msg_len);
                //     print("Error MPI_Send = " << send_ack << " " << msg);
                //     return -1;
                // }
            } else {
                // int ack = 0;
                // debug_info("MPI_Recv(" << rank << ", &ack, " << sizeof(ack) << ")");
                // auto recv_ack = MPI_Recv(&ack, 1, MPI_INT, rank, 0, comm, MPI_STATUS_IGNORE);
                // if (recv_ack != MPI_SUCCESS) {
                //     char msg[1024];
                //     int msg_len = 1024;
                //     MPI_Error_string(recv_ack, msg, &msg_len);
                //     print("Error MPI_Recv = " << recv_ack << " " << msg);
                //     return -1;
                // }
                debug_info("MPI_Send(" << rank << ", data.data(), " << std::abs(msg_size) << ")");
                auto send_msg = MPI_Isend(data.data(), std::abs(msg_size), MPI_UINT8_T, rank, 0, comm, req->get());
                if (send_msg != MPI_SUCCESS) {
                    char msg[1024];
                    int msg_len = 1024;
                    MPI_Error_string(send_msg, msg, &msg_len);
                    print("Error MPI_Send = " << send_msg << " " << msg);
                    return -1;
                }
            }
            return 0;
        };

        msg_op();
        // tpool->enqueue(msg_op);

        for (auto it = async_req.begin(); it != async_req.end();) {
            int flag = 0;

            MPI_Test((*it).get(), &flag, MPI_STATUS_IGNORE);
            if (flag) {
                it = async_req.erase(it);
            } else {
                ++it;
            }
        }
    }
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

        std::thread(echo_server, client_comm).detach();
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
