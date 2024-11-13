
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

#include "mpi.h"
#include "bw_common.hpp"
#include "lfi.h"

using namespace bw_examples;

int run_test(int id, bw_test &test)
{
    std::vector<uint8_t> data(test.test_size);
    ssize_t data_send = 0;
    ssize_t data_recv = 0;
    ssize_t test_size = test.test_size;
    timer t;
    for (size_t i = 0; i < test.test_count; i++)
    {
        data_send = lfi_send(id, data.data(), test_size);
        if (data_send != test_size)
            return -1;
        test.send_microsec += t.resetElapsedMicro();
        test.send_size += data_send;

        data_recv = lfi_recv(id, data.data(), test_size);
        if (data_recv != test_size)
            return -1;
        test.recv_microsec += t.resetElapsedMicro();
        test.recv_size += data_recv;
    }

    return 0;
}


int main(int argc, char *argv[])
{
    int ret;
    int client_fd;
    if (argc < 2)
    {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return -1;
    }

    ret = MPI_Init(&argc, &argv);
    if (ret < 0)
        exit(EXIT_FAILURE);

    print_header();


    if ((client_fd = lfi_client_create(argv[1], PORT)) < 0) {
        printf("lfi client creation error \n");
        return -1;
    }

    auto &tests = get_test_vector();

    for (auto &test : tests)
    {
        run_test(client_fd, test);
        print_test(test);
    }

    // closing the connected socket
    lfi_client_close(client_fd);

    ret = MPI_Finalize();
    if (ret < 0)
        exit(EXIT_FAILURE);

    return 0;
}