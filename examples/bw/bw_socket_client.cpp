
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

static std::vector<uint8_t> data;

int run_test(std::vector<int> sockets, bw_test &test)
{
    ssize_t data_send = 0;
    ssize_t data_recv = 0;
    ssize_t test_size = test.test_size;
    debug_info("Start run_test size "<<test.test_size);
    MPI_Barrier(MPI_COMM_WORLD);
    timer t;
    for (size_t i = 0; i < test.test_count; i++)
    {
        for (auto &socket : sockets)
        {
            debug_info("count "<<i<<" send("<<socket<<", data.data(), "<<test_size<<")");
            data_send = LFI::socket::send(socket, data.data(), test_size);
            debug_info("count "<<i<<" sended("<<socket<<", data.data(), "<<test_size<<")");
            if (data_send != test_size){
                print_error("Error count "<<i<<" socket "<<socket<<" send = "<<data_send);
                return -1;
            }
            test.size += data_send;


            int ack = 0;
            debug_info("ack recv("<<socket<<", ack, "<<sizeof(ack)<<")");
            data_recv = LFI::socket::recv(socket, &ack, sizeof(ack));
            debug_info("ack recved("<<socket<<", ack, "<<sizeof(ack)<<")");
            if (data_recv != sizeof(ack)){
                print_error("Error socket "<<socket<<" recv = "<<data_recv);
                return -1;
            }
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
    int status;
    std::vector<int> client_fds;
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

    MPI_Barrier(MPI_COMM_WORLD);
    timer t;

    client_fds.resize(servers.size());
    for (size_t i = 0; i < servers.size(); i++)
    {
        if ((client_fds[i] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        {
            printf("\n Socket creation error \n");
            return -1;
        }

        int val = 1;
        ret = setsockopt(client_fds[i], IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
        if (ret < 0) {
            print_error("ERROR: setsockopt fails");
            LFI::socket::close(client_fds[i]);
            return ret;
        }

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT);

        if (inet_pton(AF_INET, LFI::ns::get_host_ip(servers[i]).c_str(), &serv_addr.sin_addr) <= 0)
        {
            printf(
                "\nInvalid address/ Address not supported \n");
                LFI::socket::close(client_fds[i]);
            return -1;
        }

        if ((status = connect(client_fds[i], (struct sockaddr *)&serv_addr, sizeof(serv_addr))) < 0)
        {
            printf("\nConnection Failed \n");
            LFI::socket::close(client_fds[i]);
            return -1;
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0){
        print("Connection time to "<<servers.size()<<" servers: "<<(t.resetElapsedNano() * 0.000'001)<<" ms");
    }

    MPI_Barrier(MPI_COMM_WORLD);

    auto &tests = get_test_vector();
    data.resize(tests[tests.size()-1].test_size);
    
    for (auto &test : tests)
    {
        run_test(client_fds, test);
        print_test(test);
    }

    for (auto &id : client_fds)
    {
        // closing the connected socket
        close(id);
    }

    ret = MPI_Finalize();
    if (ret < 0)
        exit(EXIT_FAILURE);

    return 0;
}
