
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
#include <unistd.h>

#include <condition_variable>
#include <csignal>

#include "echo_common.hpp"
#include "impl/debug.hpp"
#include "lfi.h"
#include "mpi.h"

using namespace bw_examples;

static std::vector<int> client_fds;

#define MAX_MSG_SIZE 4 * 1024 * 1024  // 4 Mb
#define TAG_MSG      100
#define TAG_DATA     101
#define TAG_ACK      102

static std::atomic<uint64_t> test_size_global = 512 * 1024;

static std::mutex test_mutex;
static int64_t test_count_interval = 0;
static int64_t test_size_interval = 0;

static std::atomic<bool> signal_stop = false;
void signalHandler([[maybe_unused]] int signum) { signal_stop = true; }

int run_test() {
    std::condition_variable cv;
    std::vector<uint8_t> data(test_size_global.load());
    int dummy = 0;
    ssize_t data_send = 0;
    ssize_t data_recv = 0;
    debug_info("Start run_test size " << test_size_global.load());
    [[maybe_unused]] int64_t i = 0;
    while (!signal_stop) {
        std::unique_lock lock(test_mutex);
        for (auto &id : client_fds) {
            auto test_size = test_size_global.load();
            if (data.size() < test_size) data.resize(test_size);
            debug_info("msg_size " << msg_size);
            data_send = lfi_tsend(id, data.data(), test_size, TAG_DATA);
            if (data_send != static_cast<ssize_t>(test_size)) {
                print("Error lfi_send = " << data_send << " " << lfi_strerror(data_send));
                return -1;
            }

            debug_info("count " << i << " lfi_recv(" << id << ", data.data(), " << test_size << ")");
            data_recv = lfi_trecv(id, &dummy, sizeof(dummy), TAG_ACK);
            if (data_recv != sizeof(dummy)) {
                print("Error lfi_recv = " << data_recv << " " << lfi_strerror(data_recv));
                return -1;
            }
            test_count_interval++;
            test_size_interval += data_send;
        }
        cv.wait_for(lock, std::chrono::nanoseconds(0));
        i++;
    }

    debug_info("End run_test size " << test_size_global.load());
    return 0;
}

int thread_read_stdin() {
    std::string line;
    while (!signal_stop) {
        if (!std::getline(std::cin, line)) {
            // EOF o error
            signal_stop = true;
            break;
        }
        std::cout << "Leído: " << line << "\n";
        if (line == "up") {
            test_size_global = test_size_global.load() * 2;
            if (test_size_global > MAX_MSG_SIZE) test_size_global = MAX_MSG_SIZE;
        } else if (line == "down") {
            test_size_global = test_size_global.load() / 2;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int ret;

    if (argc < 2) {
        printf("Usage: %s <server_ips sep ';'>\n", argv[0]);
        return -1;
    }

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (std::signal(SIGINT, signalHandler) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
        return 1;
    }

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
        if ((client_fds[i] = lfi_client_create(servers[i].data(), PORT_LFI)) < 0) {
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

    std::thread thread(run_test);

    std::thread thread_to_read_stdin;
    if (rank == 0) {
        thread_to_read_stdin = std::thread(thread_read_stdin);
        std::cout << "Usage: write 'up' to double the msg size and 'down' to half it." << std::endl;
    }

    print_header();

    auto start = std::chrono::high_resolution_clock::now();
    while (!signal_stop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        bw_test test;
        {
            std::unique_lock lock(test_mutex);
            test.size = test_size_interval;
            test_size_interval = 0;
            auto now = std::chrono::high_resolution_clock::now();
            test.nanosec = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count();
            test.test_count = test_count_interval;
            test_count_interval = 0;
            uint64_t test_size = test_size_global.load();
            MPI_Bcast(&test_size, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
            test_size_global = test_size;
            test.test_size = test_size_global.load();
            print_test(test);
        }
        start = std::chrono::high_resolution_clock::now();
    }

    thread.join();

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

    if (rank == 0) {
        if (std::signal(SIGINT, SIG_DFL) == SIG_ERR) {
            perror("signal");
            exit(EXIT_FAILURE);
            return 1;
        }
        close(STDIN_FILENO);
        pthread_kill(thread_to_read_stdin.native_handle(), SIGINT);

        thread_to_read_stdin.join();
    }

    ret = MPI_Finalize();
    if (ret < 0) exit(EXIT_FAILURE);

    return 0;
}
