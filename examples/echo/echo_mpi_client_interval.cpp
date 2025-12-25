
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

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <condition_variable>
#include <csignal>

#include "echo_common.hpp"
#include "impl/debug.hpp"
#include "impl/ns.hpp"
#include "impl/socket.hpp"
#include "mpi.h"

using namespace bw_examples;

#define TAG_MSG 100

static std::atomic<uint64_t> test_size_global = 512 * 1024;

static std::mutex test_mutex;
static int64_t test_count_interval = 0;
static int64_t test_size_interval = 0;

static bool signal_stop = false;
void signalHandler([[maybe_unused]] int signum) { signal_stop = true; }

static MPI_Comm client_comm;
static int servers_size;

int run_test() {
    std::condition_variable cv;
    std::vector<uint8_t> data(test_size_global.load());
    int ret = 0;
    debug_info("Start run_test size " << test_size_global.load());
    [[maybe_unused]] int64_t i = 0;
    while (!signal_stop) {
        std::unique_lock lock(test_mutex);
        for (int j = 0; j < servers_size; j++) {
            auto test_size = test_size_global.load();
            if (data.size() < test_size) data.resize(test_size);
            int msg_size = test_size;
            debug_info("msg_size " << msg_size);
            ret = MPI_Send(&msg_size, 1, MPI_INT, j, TAG_MSG, client_comm);
            if (ret != MPI_SUCCESS) {
                printf("Error MPI_Send\n");
                return -1;
            }

            debug_info("count " << i << " MPI_Recv(" << data.data() << ", " << test_size << ")");
            ret = MPI_Recv(data.data(), test_size, MPI_UINT8_T, j, 0, client_comm, MPI_STATUS_IGNORE);
            if (ret != MPI_SUCCESS) {
                printf("Error MPI_Recv\n");
                return -1;
            }
            test_count_interval++;
            test_size_interval += test_size;
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
        } else if (line == "down") {
            test_size_global = test_size_global.load() / 2;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int ret;
    int status, client_fd;
    struct sockaddr_in serv_addr;
    if (argc < 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
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

    std::string port_name;
    port_name.resize(MPI_MAX_PORT_NAME);
    if (rank == 0) {
        if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            printf("Socket creation error \n");
            return -1;
        }
        serv_addr = {};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT_MPI);

        if (inet_pton(AF_INET, LFI::ns::get_host_ip(servers[0]).c_str(), &serv_addr.sin_addr) <= 0) {
            printf("Invalid address/ Address not supported \n");
            return -1;
        }
        debug_info("Connecting to " << servers[0]);
        if ((status = connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) < 0) {
            printf("Connection Failed \n");
            return -1;
        }
        debug_info("Connected to " << servers[0]);

        ret = LFI::socket::recv(client_fd, port_name.data(), MPI_MAX_PORT_NAME);
        if (ret != MPI_MAX_PORT_NAME) {
            printf("Error recv port name\n");
            return -1;
        }

        // closing the connected socket
        close(client_fd);
    }

    MPI_Bcast(port_name.data(), port_name.size(), MPI_CHAR, 0, MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    timer t;

    debug_info("Connecting mpi " << port_name.c_str());
    MPI_Comm_connect(port_name.c_str(), MPI_INFO_NULL, 0, MPI_COMM_WORLD, &client_comm);

    debug_info("Connected mpi " << port_name.c_str());
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) {
        print("Connection time to " << servers.size() << " servers: " << (t.resetElapsedNano() * 0.000'001) << " ms");
    }
    servers_size = servers.size();
    MPI_Barrier(MPI_COMM_WORLD);

    std::thread thread(run_test);
    
    if (rank == 0) {
        std::thread(thread_read_stdin).detach();
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

    if (rank == 0) {
        int disconnect = 0;
        for (int j = 0; j < servers_size; j++) {
            ret = MPI_Send(&disconnect, 1, MPI_INT, j, TAG_MSG, client_comm);
            if (ret != MPI_SUCCESS) {
                printf("Error MPI_Send disconnect\n");
                return -1;
            }
        }
    }

    MPI_Comm_disconnect(&client_comm);

    ret = MPI_Finalize();
    if (ret < 0) exit(EXIT_FAILURE);

    return 0;
}
