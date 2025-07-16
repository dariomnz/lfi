
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

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <thread>

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
struct msgs {
    void *data = nullptr;
    void *data_plugin = nullptr;
    na_op_id_t *data_op_id = nullptr;
    request_arg data_arg = {};
    void *ack = nullptr;
    void *ack_plugin = nullptr;
    na_op_id_t *ack_op_id = nullptr;
    request_arg ack_arg = {};
};

int na_init_msgs(bool is_client, msgs &out_msgs) {
    na_return_t na_ret = NA_SUCCESS;
    auto &tests = get_test_vector();
    auto max_data_size = tests[tests.size() - 1].test_size.load();
    out_msgs.data = NA_Msg_buf_alloc(na_class, max_data_size, (is_client) ? NA_SEND : NA_RECV, &out_msgs.data_plugin);
    if (out_msgs.data == nullptr) {
        printf("Error in NA_Msg_buf_alloc of data\n");
        return -1;
    }
    na_ret = NA_Msg_init_expected(na_class, out_msgs.data, max_data_size);
    if (na_ret != NA_SUCCESS) {
        print("Error NA_Msg_init_expected data = " << NA_Error_to_string(na_ret));
        return -1;
    }
    out_msgs.data_op_id = NA_Op_create(na_class, NA_OP_SINGLE);
    if (out_msgs.data_op_id == nullptr) {
        printf("Error in NA_Op_create of data\n");
        return -1;
    }
    out_msgs.ack = NA_Msg_buf_alloc(na_class, sizeof(int), (is_client) ? NA_RECV : NA_SEND, &out_msgs.ack_plugin);
    if (out_msgs.ack == nullptr) {
        printf("Error in NA_Msg_buf_alloc of data\n");
        return -1;
    }
    na_ret = NA_Msg_init_expected(na_class, out_msgs.ack, sizeof(int));
    if (na_ret != NA_SUCCESS) {
        print("Error NA_Msg_init_expected ack = " << NA_Error_to_string(na_ret));
        return -1;
    }
    out_msgs.ack_op_id = NA_Op_create(na_class, NA_OP_SINGLE);
    if (out_msgs.ack_op_id == nullptr) {
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

int run_test(na_addr_t *na_addr_peer, bw_test &test, msgs &msg) {
    na_return_t na_ret = NA_SUCCESS;
    ssize_t test_size = test.test_size;
    debug_info("Start run_test size " << test.test_size);
    for (size_t i = 0; i < test.test_count; i++) {
        debug_info("count " << i << " NA_Msg_recv_unexpected(data.data(), " << test_size << ")");
        msg.data_arg.completed = 1;
        na_ret = NA_Msg_recv_expected(na_class, na_context, na_request_complete, &msg.data_arg, msg.data, test_size,
                                      msg.data_plugin, na_addr_peer, 0, 0, msg.data_op_id);
        if (na_ret != NA_SUCCESS) {
            print("Error NA_Msg_recv_expected " << i << " = " << test_size << " " << NA_Error_to_string(na_ret));
            return -1;
        }
        if (wait(msg.data_arg) < 0) {
            print("Error wait");
            return -1;
        }
        debug_info("ack MPI_Send(ack, " << sizeof(int) << ")");
        msg.ack_arg.completed = 1;
        na_ret = NA_Msg_send_expected(na_class, na_context, na_request_complete, &msg.ack_arg, msg.ack, sizeof(int),
                                      msg.ack_plugin, na_addr_peer, 0, 0, msg.ack_op_id);
        if (na_ret != NA_SUCCESS) {
            print("Error NA_Msg_send_expected " << i << " = " << test_size << " " << NA_Error_to_string(na_ret));
            return -1;
        }
        if (wait(msg.ack_arg) < 0) {
            print("Error wait");
            return -1;
        }
    }
    debug_info("End run_test size " << test.test_size);

    return 0;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char *argv[]) {
    const int max_clients = 1024;
    int server_fd = -1, new_socket;
    struct sockaddr_in address = {};
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    na_return_t na_ret = NA_SUCCESS;
    NA_Set_log_level("debug");
    na_class = NA_Initialize("psm2", false);
    if (na_class == nullptr) {
        printf("Error NA_Initialize\n");
        exit(EXIT_FAILURE);
    }

    na_context = NA_Context_create(na_class);
    if (na_context == nullptr) {
        printf("Error NA_Context_create\n");
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

    printf("Addr len: %ld Addr: %s\n", addr_string_len, addr_string);

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Forcefully attaching socket to the port 8080
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    auto &tests = get_test_vector();

    debug_info("Server start accepting " << LFI::ns::get_host_name() << " :");
    int iter = 0;
    while (iter < max_clients) {
        print("Server start accepting");
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        [[maybe_unused]] auto send_port = LFI::socket::send(new_socket, addr_string, NA_TEST_MAX_ADDR_NAME);
        debug_info("Send addr string " << send_port);
        char addr_string_peer[NA_TEST_MAX_ADDR_NAME] = {};
        [[maybe_unused]] auto recv_port = LFI::socket::recv(new_socket, addr_string_peer, NA_TEST_MAX_ADDR_NAME);
        debug_info("Recv addr string " << recv_port);
        na_addr_t *na_addr_peer = nullptr;
        na_ret = NA_Addr_lookup(na_class, addr_string_peer, &na_addr_peer);
        if (na_ret != NA_SUCCESS) {
            print("Error NA_Addr_lookup " << NA_Error_to_string(na_ret));
            exit(EXIT_FAILURE);
        }
        int sock_ack = 0;
        LFI::socket::send(new_socket, &sock_ack, sizeof(sock_ack));
        LFI::socket::recv(new_socket, &sock_ack, sizeof(sock_ack));
        close(new_socket);
        print("Server accept client " << iter << " " << addr_string_peer);

        std::thread([na_addr_peer, &tests]() {
            int ret = 0;
            msgs msg = {};
            if (na_init_msgs(false, msg) < 0) {
                print("Error in na_init_msgs");
                exit(EXIT_FAILURE);
            }
            for (auto &test : tests) {
                ret = run_test(na_addr_peer, test, msg);
                if (ret < 0) break;
            }
            NA_Addr_free(na_class, na_addr_peer);
        }).detach();
        iter++;
    }
    // closing the listening socket
    close(server_fd);

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

    return 0;
}
