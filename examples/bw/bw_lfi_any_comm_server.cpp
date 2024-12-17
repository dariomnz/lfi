
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

#include <thread>
#include "mpi.h"
#include "bw_common.hpp"
#include "lfi.h"
#include "impl/debug.hpp"
#include "impl/ns.hpp"

using namespace bw_examples;

int run_test(int id, bw_test &test)
{
    std::vector<uint8_t> data(test.test_size);
    ssize_t data_send = 0;
    ssize_t data_recv = 0;
    ssize_t test_size = test.test_size;
    debug_info("Start run_test id " << id << " size " << test.test_size);
    for (size_t i = 0; i < test.test_count; i++)
    {
        debug_info("count " << i << " lfi_recv(" << id << ", data.data(), " << test_size << ")");
        data_recv = lfi_trecv(id, data.data(), test_size, test.test_tag);
        if (data_recv != test_size)
        {
            print("Error lfi_recv = " << data_recv << " " << lfi_strerror(data_recv));
            return -1;
        }
    }
    int ack = 0;
    debug_info("ack lfi_send(" << id << ", ack, " << sizeof(ack) << ")");
    data_send = lfi_tsend(id, &ack, sizeof(ack), test.test_tag);
    if (data_send != sizeof(ack))
    {
        print("Error lfi_send = " << data_send << " " << lfi_strerror(data_send));
        return -1;
    }

    debug_info("End run_test id " << id << " size " << test.test_size);

    return 0;
}

int main(int argc, char *argv[])
{
    int ret;
    const int max_clients = 1024;
    int server_fd, new_socket;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    ret = MPI_Init(&argc, &argv);
    if (ret < 0)
        exit(EXIT_FAILURE);

    // Creating socket file descriptor
    int port = PORT;
    if ((server_fd = lfi_server_create(NULL, &port)) < 0)
    {
        perror("lfi server failed");
        exit(EXIT_FAILURE);
    }

    auto &tests = get_test_vector();

    print("Server start accepting " << LFI::ns::get_host_name() << " :");
    int iter = 0;
    std::atomic_int clients_to_launch_thread = 0;
    std::thread([&tests, &clients_to_launch_thread](){
        int ack = 0;
        int iter = 0;
        while(iter < max_clients){
            while(clients_to_launch_thread == 0){
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            clients_to_launch_thread--;
            print("Start recv any ack");
            int source = -1;
            ssize_t any_recv = lfi_any_recv(&ack, sizeof(ack), &source);
            if (any_recv != sizeof(ack)){
                print("Error lfi_any_recv = " << any_recv << " " << lfi_strerror(any_recv));
                exit(1);
            }
            print("Start test for client "<<source);
            std::thread([id = source, &tests](){
                int ret = 0;
                for (auto &test : tests)
                {
                    ret = run_test(id, test);
                    if (ret < 0) break;
                }
                lfi_client_close(id);
            }).detach(); 
        }
        }).detach();
    while (iter < max_clients)
    {
        if ((new_socket = lfi_server_accept(server_fd)) < 0)
        {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        print("Server accept client " << new_socket);
        clients_to_launch_thread++;
        // std::thread([new_socket, &tests]()
        //             {
        //     int ret = 0;
        //     for (auto &test : tests)
        //     {
        //         ret = run_test(new_socket, test);
        //         if (ret < 0) break;
        //     }
        //     lfi_client_close(new_socket); })
        //     .detach();
        iter++;
    }
    // closing the listening socket
    lfi_server_close(server_fd);

    ret = MPI_Finalize();
    if (ret < 0)
        exit(EXIT_FAILURE);

    return 0;
}
