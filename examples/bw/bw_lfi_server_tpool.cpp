
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
#include "bw_common.hpp"
#include "impl/debug.hpp"
#include "impl/ns.hpp"
#include "lfi.h"
#include "mpi.h"
#include "thread_pool.hpp"

using namespace bw_examples;

static std::vector<uint8_t> data;

int run_test(int id, bw_test &test, ThreadPool &tpool) {
    ssize_t test_size = test.test_size;
    debug_info("Start run_test id " << id << " size " << test.test_size);
    std::vector<std::future<int>> v_futs;
    v_futs.reserve(test.test_count);
    // std::this_thread::sleep_for(std::chrono::seconds(12));
    v_futs.emplace_back(tpool.enqueue([id, test_size, &test]() {
        for (size_t i = 0; i < test.test_count; i++) {
            ssize_t data_send = 0;
            ssize_t data_recv = 0;

            debug_info("count " << i << " lfi_recv(" << id << ", data.data(), " << test_size << ")");
            data_recv = lfi_recv(id, data.data(), test_size);
            if (data_recv != test_size) {
                print("Error lfi_recv = " << data_recv << " " << lfi_strerror(data_recv));
                return -1;
            }

            // simulate work
            volatile int acum = 0;
            for (size_t w = 0; w < 1'000'000; w++) {
                acum += w;
            }

            int ack = 0;
            debug_info("ack lfi_send(" << id << ", ack, " << sizeof(ack) << ")");
            data_send = lfi_send(id, &ack, sizeof(ack));
            if (data_send != sizeof(ack)) {
                print("Error lfi_send = " << data_send << " " << lfi_strerror(data_send));
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

    debug_info("End run_test id " << id << " size " << test.test_size);

    return 0;
}

int main(int argc, char *argv[]) {
    int ret;
    const int max_clients = 1024;
    int server_fd, new_socket;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    ret = MPI_Init(&argc, &argv);
    if (ret < 0) exit(EXIT_FAILURE);

    // Creating socket file descriptor
    int port = PORT;
    if ((server_fd = lfi_server_create(NULL, &port)) < 0) {
        perror("lfi server failed");
        exit(EXIT_FAILURE);
    }

    auto &tests = get_test_vector();
    data.resize(tests[tests.size() - 1].test_size);

    ThreadPool tpool{};

    print("Server start accepting " << LFI::ns::get_host_name() << " :");
    int iter = 0;
    while (iter < max_clients) {
        if ((new_socket = lfi_server_accept(server_fd)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        print("Server accept client " << new_socket);
        std::thread([new_socket, &tests, &tpool]() {
            int ret = 0;
            for (auto &test : tests) {
                ret = run_test(new_socket, test, tpool);
                if (ret < 0) break;
            }
            lfi_client_close(new_socket);
        }).detach();
        iter++;
    }
    // closing the listening socket
    lfi_server_close(server_fd);

    ret = MPI_Finalize();
    if (ret < 0) exit(EXIT_FAILURE);

    return 0;
}
