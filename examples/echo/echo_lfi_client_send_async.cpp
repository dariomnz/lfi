
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
#include "echo_common.hpp"
#include "impl/debug.hpp"
#include "lfi.h"
#include "lfi_async.h"
#include "lfi_error.h"
#include "mpi.h"

using namespace bw_examples;

static std::vector<uint8_t> data;

#define TAG_MSG 100

int run_test(std::vector<int> &ids, bw_test &test) {
    ssize_t ret = 0;
    ssize_t test_size = test.test_size;
    debug_info("Start run_test size " << test.test_size);
    std::vector<std::unique_ptr<lfi_request, void (*)(lfi_request *)>> v_unique_reqs;
    std::vector<lfi_request *> v_reqs;
    v_unique_reqs.reserve(test.test_count * ids.size() * 2);
    v_reqs.reserve(test.test_count * ids.size() * 2);
    // std::this_thread::sleep_for(std::chrono::seconds(10));
    test.size = 0;
    MPI_Barrier(MPI_COMM_WORLD);
    timer t;
    for (size_t i = 0; i < test.test_count; i++) {
        for (auto &id : ids) {
            int msg_size = -test_size;
            debug_info("msg_size " << msg_size);
            auto &unique_req1 = v_unique_reqs.emplace_back(lfi_request_create(id), lfi_request_free);
            auto &req1 = v_reqs.emplace_back(unique_req1.get());
            ret = lfi_tsend_async(req1, &msg_size, sizeof(msg_size), TAG_MSG);
            if (ret != LFI_SUCCESS) {
                print("Error lfi_send = " << ret << " " << lfi_strerror(ret));
                return -1;
            }

            // std::this_thread::sleep_for(std::chrono::milliseconds(100));
            debug_info("count " << i << " lfi_send(" << id << ", data.data(), " << test_size << ")");
            auto &unique_req2 = v_unique_reqs.emplace_back(lfi_request_create(id), lfi_request_free);
            auto &req2 = v_reqs.emplace_back(unique_req2.get());
            ret = lfi_send_async(req2, data.data(), test_size);
            if (ret != LFI_SUCCESS) {
                print("Error lfi_send = " << ret << " " << lfi_strerror(ret));
                return -1;
            }
            test.size += test_size;
            // int ack = 0;
            // debug_info("count " << i << " lfi_recv(" << id << ", ack, " << sizeof(ack) << ")");
            // auto &unique_req3 = v_unique_reqs.emplace_back(lfi_request_create(id), lfi_request_free);
            // auto &req3 = v_reqs.emplace_back(unique_req3.get());
            // ret = lfi_recv_async(req3, &ack, sizeof(ack));
            // if (ret != LFI_SUCCESS) {
            //     print("Error lfi_recv = " << ret << " " << lfi_strerror(ret));
            //     return -1;
            // }
        }
        auto res = lfi_wait_all(v_reqs.data(), v_reqs.size());
        if (res < 0) {
            print("Error lfi_wait_all = " << res << " " << lfi_strerror(res));
            return -1;
        }

        v_unique_reqs.clear();
        v_reqs.clear();
    }

    MPI_Barrier(MPI_COMM_WORLD);
    test.nanosec = t.resetElapsedNano();

    debug_info("End run_test size " << test.test_size);

    return 0;
}

int main(int argc, char *argv[]) {
    int ret;
    std::vector<int> client_fds;

    if (argc < 2) {
        printf("Usage: %s <server_ips sep ';'>\n", argv[0]);
        return -1;
    }

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    auto servers = split(argv[1], ";");

    ret = MPI_Init(&argc, &argv);
    if (ret < 0) exit(EXIT_FAILURE);

    int rank;
    ret = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (ret < 0) exit(EXIT_FAILURE);

    MPI_Barrier(MPI_COMM_WORLD);
    timer t;

    client_fds.resize(servers.size());
    for (size_t i = 0; i < servers.size(); i++) {
        if ((client_fds[i] = lfi_client_create(servers[i].data(), PORT)) < 0) {
            printf("lfi client creation error \n");
            MPI_Abort(MPI_COMM_WORLD, -1);
            return -1;
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) {
        print("Connection time to " << servers.size() << " servers: " << (t.resetElapsedNano() * 0.000'001) << " ms");
    }

    MPI_Barrier(MPI_COMM_WORLD);

    auto &tests = get_test_vector();
    data.resize(tests[tests.size() - 1].test_size);
    print_header();

    const int RERUN_TEST = 1;
    for (auto &test : tests) {
        for (int i = 0; i < RERUN_TEST; i++) {
            ret = run_test(client_fds, test);
            if (ret < 0) {
                MPI_Abort(MPI_COMM_WORLD, -1);
                break;
            }
            print_test(test);
        }
    }

    for (auto &id : client_fds) {
        int disconnect = 0;
        auto data_send = lfi_tsend(id, &disconnect, sizeof(disconnect), TAG_MSG);
        if (data_send < 0) {
            print("Error lfi_recv = " << data_send << " " << lfi_strerror(data_send));
            return -1;
        }
        // closing the connected socket
        lfi_client_close(id);
    }

    ret = MPI_Finalize();
    if (ret < 0) exit(EXIT_FAILURE);

    return 0;
}
