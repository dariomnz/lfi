
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

#include "mpi.h"
#include "bw_common.hpp"
#include "lfi.h"
#include "lfi_async.h"
#include "impl/debug.hpp"

using namespace bw_examples;

int run_test(std::vector<int>& ids, bw_test &test)
{
    std::vector<uint8_t> data(test.test_size);
    ssize_t data_send = 0;
    ssize_t data_recv = 0;
    ssize_t test_size = test.test_size;
    debug_info("Start run_test size "<<test.test_size);
    std::vector<lfi_request*> requests;
    requests.reserve(test.test_count*ids.size()*2);
    MPI_Barrier(MPI_COMM_WORLD);
    timer t;
    int ack = 0;
    for (size_t i = 0; i < test.test_count; i++)
    {
        for (auto &id : ids)
        {
            auto& req = requests.emplace_back(lfi_request_create(id));
            debug_info("count "<<i<<" lfi_send("<<id<<", data.data(), "<<test_size<<")");
            data_send = lfi_send_async(req, data.data(), test_size);
            if (data_send < 0){
                print("Error lfi_send = "<<data_send<<" "<<lfi_strerror(data_send));
                return -1;
            }
            test.size += test_size;
            auto& req_ack = requests.emplace_back(lfi_request_create(id));
            debug_info("ack lfi_recv("<<id<<", ack, "<<sizeof(ack)<<")");
            data_recv = lfi_recv_async(req_ack, &ack, sizeof(ack));
            if (data_recv < 0){
                print("Error lfi_recv = "<<data_recv<<" "<<lfi_strerror(data_recv));
                return -1;
            }
        }
    }
    lfi_wait_all(requests.data(), requests.size());

    MPI_Barrier(MPI_COMM_WORLD);
    test.nanosec += t.resetElapsedNano();
    
    debug_info("End run_test size "<<test.test_size);

    for (auto &req : requests)
    {
        lfi_request_free(req);
    }

    return 0;
}


int main(int argc, char *argv[])
{
    int ret;
    std::vector<int> client_fds;

    if (argc < 2)
    {
        printf("Usage: %s <server_ips sep ';'>\n", argv[0]);
        return -1;
    }

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    auto servers = split(argv[1], ";");

    ret = MPI_Init(&argc, &argv);
    if (ret < 0)
        exit(EXIT_FAILURE);

    client_fds.resize(servers.size());
    for (size_t i = 0; i < servers.size(); i++)
    {
        if ((client_fds[i] = lfi_client_create(servers[i].data(), PORT)) < 0) {
            printf("lfi client creation error \n");
            MPI_Abort(MPI_COMM_WORLD, -1);
            return -1;
        }
    }
    

    MPI_Barrier(MPI_COMM_WORLD);

    auto &tests = get_test_vector();
    print_header();

    for (auto &test : tests)
    {
        ret = run_test(client_fds, test);
        if (ret < 0){
            MPI_Abort(MPI_COMM_WORLD, -1);
            break;
        }
        print_test(test);
    }

    for (auto &id : client_fds)
    {
        // closing the connected socket
        lfi_client_close(id);
    }
    

    ret = MPI_Finalize();
    if (ret < 0)
        exit(EXIT_FAILURE);

    return 0;
}
