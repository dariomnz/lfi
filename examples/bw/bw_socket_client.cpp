
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
#include "impl/ns.hpp"

using namespace bw_examples;

int run_test(int socket, bw_test &test)
{
    std::vector<uint8_t> data(test.test_size);
    ssize_t data_send = 0;
    ssize_t data_recv = 0;
    ssize_t test_size = test.test_size;
    MPI_Barrier(MPI_COMM_WORLD);
    timer t;
    for (size_t i = 0; i < test.test_count; i++)
    {
        data_send = LFI::socket::send(socket, data.data(), test_size);
        if (data_send != test_size)
            return -1;
        test.size += data_send;
    }
    
    int ack = 0;
    data_recv = LFI::socket::recv(socket, &ack, sizeof(ack));
    if (data_recv != sizeof(ack))
        return -1;

    MPI_Barrier(MPI_COMM_WORLD);
    test.nanosec += t.resetElapsedNano();

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

    ret = MPI_Init(&argc, &argv);
    if (ret < 0)
        exit(EXIT_FAILURE);

    print_header();

    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, LFI::ns::get_host_ip(argv[1]).c_str(), &serv_addr.sin_addr) <= 0)
    {
        printf(
            "\nInvalid address/ Address not supported \n");
        return -1;
    }

    if ((status = connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) < 0)
    {
        printf("\nConnection Failed \n");
        return -1;
    }
    
    MPI_Barrier(MPI_COMM_WORLD);

    auto &tests = get_test_vector();

    for (auto &test : tests)
    {
        run_test(client_fd, test);
        print_test(test);
    }

    // closing the connected socket
    close(client_fd);

    ret = MPI_Finalize();
    if (ret < 0)
        exit(EXIT_FAILURE);

    return 0;
}
