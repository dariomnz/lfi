
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

#include "bw_common.hpp"
#include "impl/debug.hpp"
#include "impl/ns.hpp"
#include "impl/socket.hpp"
#include "mpi.h"
#include "na.h"

#define NA_TEST_MAX_ADDR_NAME 1024
using namespace bw_examples;

struct request_arg {
    std::atomic_int completed = 0;
};

void na_request_complete([[maybe_unused]] const struct na_cb_info *na_cb_info) {
    auto req_arg = reinterpret_cast<request_arg *>(na_cb_info->arg);
    req_arg->completed -= 1;
}

static na_class_t *na_class = nullptr;
static na_context_t *na_context = nullptr;
static void *data;
static void *data_plugin;
static na_op_id_t *data_op_id;
static request_arg data_arg = {};
static void *ack;
static void *ack_plugin;
static na_op_id_t *ack_op_id;
static request_arg ack_arg = {};

int na_init_msgs(bool is_client) {
    na_return_t na_ret = NA_SUCCESS;
    auto &tests = get_test_vector();
    auto max_data_size = tests[tests.size() - 1].test_size.load();
    data = NA_Msg_buf_alloc(na_class, max_data_size, (is_client) ? NA_SEND : NA_RECV, &data_plugin);
    if (data == nullptr) {
        printf("Error in NA_Msg_buf_alloc of data\n");
        return -1;
    }
    na_ret = NA_Msg_init_expected(na_class, data, max_data_size);
    if (na_ret != NA_SUCCESS) {
        print("Error NA_Msg_init_expected data = " << NA_Error_to_string(na_ret));
        return -1;
    }
    data_op_id = NA_Op_create(na_class, NA_OP_SINGLE);
    if (data_op_id == nullptr) {
        printf("Error in NA_Op_create of data\n");
        return -1;
    }
    ack = NA_Msg_buf_alloc(na_class, sizeof(int), (is_client) ? NA_RECV : NA_SEND, &ack_plugin);
    if (ack == nullptr) {
        printf("Error in NA_Msg_buf_alloc of data\n");
        return -1;
    }
    na_ret = NA_Msg_init_expected(na_class, ack, sizeof(int));
    if (na_ret != NA_SUCCESS) {
        print("Error NA_Msg_init_expected ack = " << NA_Error_to_string(na_ret));
        return -1;
    }
    ack_op_id = NA_Op_create(na_class, NA_OP_SINGLE);
    if (ack_op_id == nullptr) {
        printf("Error in NA_Op_create of ack\n");
        return -1;
    }
    return 0;
}

int wait(request_arg &req_arg) {
    na_return_t na_ret = NA_SUCCESS;
    do {
        unsigned int count = 0;
        unsigned int actual_count = 0;
        na_ret = NA_Poll(na_class, na_context, &count);
        if (na_ret != NA_SUCCESS) {
            print("Error NA_Poll ack = " << NA_Error_to_string(na_ret));
            return -1;
        }

        if (count == 0) continue;

        na_ret = NA_Trigger(na_context, count, &actual_count);
        if (na_ret != NA_SUCCESS) {
            print("Error NA_Trigger ack = " << NA_Error_to_string(na_ret));
            return -1;
        }
    } while (req_arg.completed > 0);
    return 0;
}

int run_test(std::vector<na_addr_t *> &ids, bw_test &test) {
    na_return_t na_ret = NA_SUCCESS;
    ssize_t data_send = 0;
    ssize_t test_size = test.test_size;
    debug_info("Start run_test size " << test.test_size);
    MPI_Barrier(MPI_COMM_WORLD);
    timer t;
    for (size_t i = 0; i < test.test_count; i++) {
        for (auto &id : ids) {
            debug_info("count " << i << " lfi_send(" << id << ", data.data(), " << test_size << ")");
            data_arg.completed = 1;
            na_ret = NA_Msg_send_expected(na_class, na_context, na_request_complete, &data_arg, data, test_size,
                                          data_plugin, id, 0, 0, data_op_id);
            data_send = test_size;
            if (na_ret != NA_SUCCESS) {
                print("Error NA_Msg_send_unexpected " << i << " = " << data_send << " " << NA_Error_to_string(na_ret));
                return -1;
            }
            if (wait(data_arg) < 0) {
                print("Error wait");
                return -1;
            }
            test.size += data_send;
            debug_info("ack lfi_recv(" << id << ", ack, " << sizeof(int) << ")");
            ack_arg.completed = 1;
            na_ret = NA_Msg_recv_expected(na_class, na_context, na_request_complete, &ack_arg, ack, sizeof(int),
                                          ack_plugin, id, 0, 0, ack_op_id);
            if (na_ret != NA_SUCCESS) {
                print("Error NA_Msg_recv_unexpected " << i << " = " << data_send << " " << NA_Error_to_string(na_ret));
                return -1;
            }
            if (wait(ack_arg) < 0) {
                print("Error wait");
                return -1;
            }
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    test.nanosec += t.resetElapsedNano();

    debug_info("End run_test size " << test.test_size);

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

    ret = MPI_Init(&argc, &argv);
    if (ret < 0) exit(EXIT_FAILURE);

    int rank;
    ret = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (ret < 0) exit(EXIT_FAILURE);

    MPI_Barrier(MPI_COMM_WORLD);
    timer t;
    auto servers = split(argv[1], ";");

    na_return_t na_ret = NA_SUCCESS;
    na_class = NA_Initialize("psm2", false);
    if (na_class == nullptr) {
        print("Error NA_Initialize " << NA_Error_to_string(na_ret));
        exit(EXIT_FAILURE);
    }
    NA_Set_log_level("debug");

    na_context = NA_Context_create(na_class);
    if (na_context == nullptr) {
        print("Error NA_Context_create " << NA_Error_to_string(na_ret));
        exit(EXIT_FAILURE);
    }
    na_addr_t *na_addr = nullptr;
    na_ret = NA_Addr_self(na_class, &na_addr);
    if (na_ret != NA_SUCCESS) {
        print("Error NA_Addr_self " << NA_Error_to_string(na_ret));
        exit(EXIT_FAILURE);
    }

    char addr_string[NA_TEST_MAX_ADDR_NAME] = {};
    size_t addr_string_len = NA_TEST_MAX_ADDR_NAME;
    na_ret = NA_Addr_to_string(na_class, addr_string, &addr_string_len, na_addr);
    if (na_ret != NA_SUCCESS) {
        print("Error NA_Addr_to_string " << NA_Error_to_string(na_ret));
        exit(EXIT_FAILURE);
    }

    debug_info("Addr len: " << addr_string_len << " Addr: " << addr_string);

    print_header();
    std::vector<na_addr_t *> ids;
    for (auto &&srv : servers) {
        print("Rank: " << rank << " Server: " << srv);
        char addr_string_peer[NA_TEST_MAX_ADDR_NAME] = {};
        if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            printf("Socket creation error \n");
            return -1;
        }
        serv_addr = {};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT);

        if (inet_pton(AF_INET, LFI::ns::get_host_ip(srv).c_str(), &serv_addr.sin_addr) <= 0) {
            printf("Invalid address/ Address not supported \n");
            return -1;
        }
        debug_info("Connecting to " << srv);
        if ((status = connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) < 0) {
            printf("Connection Failed \n");
            return -1;
        }
        debug_info("Connected to " << srv);

        ret = LFI::socket::recv(client_fd, addr_string_peer, NA_TEST_MAX_ADDR_NAME);
        debug_info("Recv addr string " << ret);
        if (ret != NA_TEST_MAX_ADDR_NAME) {
            printf("Error recv port name\n");
            return -1;
        }

        na_addr_t *na_addr_peer = nullptr;
        na_ret = NA_Addr_lookup(na_class, addr_string_peer, &na_addr_peer);
        if (na_ret != NA_SUCCESS) {
            print("Error NA_Addr_lookup " << NA_Error_to_string(na_ret));
            exit(EXIT_FAILURE);
        }

        ids.emplace_back(na_addr_peer);

        ret = LFI::socket::send(client_fd, addr_string, NA_TEST_MAX_ADDR_NAME);
        debug_info("Send addr string " << ret);
        if (ret != NA_TEST_MAX_ADDR_NAME) {
            printf("Error recv port name\n");
            return -1;
        }

        int sock_ack = 0;
        LFI::socket::recv(client_fd, &sock_ack, sizeof(sock_ack));
        LFI::socket::send(client_fd, &sock_ack, sizeof(sock_ack));
        // closing the connected socket
        close(client_fd);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    if (rank == 0) {
        print("Connection time to " << servers.size() << " servers: " << (t.resetElapsedNano() * 0.000'001) << " ms");
    }

    MPI_Barrier(MPI_COMM_WORLD);

    auto &tests = get_test_vector();

    if (na_init_msgs(true) < 0) {
        printf("Error na_init_msgs\n");
        exit(EXIT_FAILURE);
    }

    for (auto &test : tests) {
        run_test(ids, test);
        print_test(test);
    }

    for (auto &&id : ids) {
        NA_Addr_free(na_class, id);
    }
    NA_Addr_free(na_class, na_addr);

    na_ret = NA_Context_destroy(na_class, na_context);
    if (na_ret != NA_SUCCESS) {
        print("Error NA_Context_destroy " << NA_Error_to_string(na_ret));
        exit(EXIT_FAILURE);
    }

    na_ret = NA_Finalize(na_class);
    if (na_ret != NA_SUCCESS) {
        print("Error NA_Finalize " << NA_Error_to_string(na_ret));
        exit(EXIT_FAILURE);
    }

    ret = MPI_Finalize();
    if (ret < 0) exit(EXIT_FAILURE);

    return 0;
}
