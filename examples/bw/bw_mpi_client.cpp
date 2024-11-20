
/*
 *  Copyright 2024-2022 Dario Muñoz Muñoz, Felix Garcia Carballeira, Diego Camarmas Alonso, Alejandro Calderon Mateos
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

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "mpi.h"
#include "bw_common.hpp"
#include "impl/socket.hpp"
#include "impl/debug.hpp"
#include "impl/ns.hpp"

using namespace bw_examples;

int run_test(MPI_Comm& client_comm, int servers, bw_test &test)
{
    std::vector<uint8_t> data(test.test_size);
    int ret = 0;
    ssize_t test_size = test.test_size;
    debug_info("Start run_test size "<<test.test_size);
    MPI_Barrier(MPI_COMM_WORLD);
    timer t;
    for (size_t i = 0; i < test.test_count; i++)
    {
        for (int j = 0; j < servers; j++)
        {
            debug_info("count "<<i<<" MPI_Send(data.data(), "<<test_size<<")");
            ret = MPI_Send(data.data(), test_size, MPI_UINT8_T, j, 0, client_comm);
            if (ret != MPI_SUCCESS){
                printf("Error MPI_Send\n");
                return -1;
            }
            test.size += test_size;
        }
    }
    for (int j = 0; j < servers; j++)
    {
        int ack = 0;
        debug_info("ack MPI_Recv(ack, "<<sizeof(ack)<<")");
        ret = MPI_Recv(&ack, 1, MPI_INT, j, 0, client_comm, MPI_STATUS_IGNORE);
        if (ret != MPI_SUCCESS){
            printf("Error MPI_Recv\n");
            return -1;
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    test.nanosec += t.resetElapsedNano();

    debug_info("End run_test size "<<test.test_size);

    return 0;
}


int main(int argc, char *argv[])
{
    int ret;
    int status, client_fd;
    struct sockaddr_in serv_addr;
    if (argc < 2)
    {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return -1;
    }

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    auto servers = split(argv[1], ";");

    ret = MPI_Init(&argc, &argv);
    if (ret < 0)
        exit(EXIT_FAILURE);

    print_header();

    int rank;
    ret = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (ret < 0)
        exit(EXIT_FAILURE);

    MPI_Comm client_comm;

    std::string port_name;
    port_name.resize(MPI_MAX_PORT_NAME);
    if (rank == 0){
        if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            printf("Socket creation error \n");
            return -1;
        }
        serv_addr = {};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT);

        if (inet_pton(AF_INET, LFI::ns::get_host_ip(servers[0]).c_str(), &serv_addr.sin_addr) <= 0)
        {
            printf("Invalid address/ Address not supported \n");
            return -1;
        }
        debug_info("Connecting to "<<servers[0]);
        if ((status = connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) < 0)
        {
            printf("Connection Failed \n");
            return -1;
        }
        debug_info("Connected to "<<servers[0]);

        ret = LFI::socket::recv(client_fd, port_name.data(), MPI_MAX_PORT_NAME);
        if (ret != MPI_MAX_PORT_NAME){
            printf("Error recv port name\n");
            return -1;
        }

        // closing the connected socket
        close(client_fd);
    }

    MPI_Bcast(port_name.data(), port_name.size(), MPI_CHAR, 0, MPI_COMM_WORLD);

    debug_info("Connecting mpi "<<port_name.c_str());
    MPI_Comm_connect(port_name.c_str(), MPI_INFO_NULL, 0, MPI_COMM_WORLD, &client_comm);
    
    debug_info("Connected mpi "<<port_name.c_str());

    MPI_Barrier(MPI_COMM_WORLD);

    auto &tests = get_test_vector();

    for (auto &test : tests)
    {
        run_test(client_comm, servers.size(), test);
        print_test(test);
    }

    MPI_Comm_disconnect(&client_comm);

    ret = MPI_Finalize();
    if (ret < 0)
        exit(EXIT_FAILURE);

    return 0;
}
