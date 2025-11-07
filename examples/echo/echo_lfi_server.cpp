
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

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

#include "bw_common.hpp"
#include "impl/debug.hpp"
#include "impl/ns.hpp"
#include "lfi.h"
#include "lfi_async.h"
#include "thread_pool.hpp"

using namespace bw_examples;

#define TAG_MSG 100

static std::unique_ptr<ThreadPool> tpool = std::make_unique<ThreadPool>(4);
static std::atomic<int> clients = 0;

void echo_server() {
    int msg_size_shm = 0;
    int msg_size_peer = 0;
    std::unique_ptr<lfi_request, void (*)(lfi_request *)> shm_request(lfi_request_create(LFI_ANY_COMM_SHM),
                                                                      lfi_request_free);
    if (lfi_trecv_async(shm_request.get(), &msg_size_shm, sizeof(msg_size_shm), TAG_MSG) < 0) {
        print("Error in lfi_trecv_async");
        return;
    }
    std::unique_ptr<lfi_request, void (*)(lfi_request *)> peer_request(lfi_request_create(LFI_ANY_COMM_PEER),
                                                                       lfi_request_free);
    if (lfi_trecv_async(peer_request.get(), &msg_size_peer, sizeof(msg_size_peer), TAG_MSG) < 0) {
        print("Error in lfi_trecv_async");
        return;
    }
    while (true) {
        while (clients.load() <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        debug_info("Start recv any ack");

        lfi_request *requests[2] = {shm_request.get(), peer_request.get()};

        int completed = lfi_wait_any(requests, 2);
        int source = -1;
        int msg_size = 0;
        int error = 0;
        debug_info("Completed wait_num with " << completed);
        if (completed == 0) {
            source = lfi_request_source(shm_request.get());
            msg_size = msg_size_shm;
            error = lfi_request_error(shm_request.get());
            debug_info("readed shm " << msg_size_shm);
            // Reuse the same request
            if (lfi_trecv_async(shm_request.get(), &msg_size_shm, sizeof(msg_size_shm), TAG_MSG) < 0) {
                print("Error in lfi_trecv_async");
                return;
            }
        } else if (completed == 1) {
            source = lfi_request_source(peer_request.get());
            msg_size = msg_size_peer;
            error = lfi_request_error(peer_request.get());
            debug_info("readed peer " << msg_size_peer);
            // Reuse the same request
            if (lfi_trecv_async(peer_request.get(), &msg_size_peer, sizeof(msg_size_peer), TAG_MSG) < 0) {
                print("Error in lfi_trecv_async");
                return;
            }
        } else {
            print("Error in wait_num");
            return;
        }

        if (error < 0) {
            print("Receive error in comm " << source << " : " << lfi_strerror(error));
            print("Server disconnect client " << source);
            lfi_client_close(source);
            clients--;
            continue;
        }

        if (msg_size == 0) {
            print("Server disconnect client " << source);
            lfi_client_close(source);
            clients--;
            continue;
        }

        auto msg_op = [msg_size, id = source]() {
            std::vector<uint8_t> data;
            data.resize(std::abs(msg_size));
            if (msg_size < 0) {
                debug_info("lfi_recv(" << id << ", data.data(), " << std::abs(msg_size) << ")");
                auto recv_msg = lfi_recv(id, data.data(), std::abs(msg_size));
                if (recv_msg < 0) {
                    print("Error lfi_recv(" << id << ") = " << recv_msg << " " << lfi_strerror(recv_msg));
                    lfi_client_close(id);
                    return -1;
                }
                int ack = 0;
                debug_info("lfi_send(" << id << ", &ack, " << sizeof(ack) << ")");
                auto send_ack = lfi_send(id, &ack, sizeof(ack));
                if (send_ack < 0) {
                    print("Error lfi_send(" << id << ") = " << send_ack << " " << lfi_strerror(send_ack));
                    lfi_client_close(id);
                    return -1;
                }
            } else {
                int ack = 0;
                debug_info("lfi_recv(" << id << ", &ack, " << sizeof(ack) << ")");
                auto recv_ack = lfi_recv(id, &ack, sizeof(ack));
                if (recv_ack < 0) {
                    print("Error lfi_recv(" << id << ") = " << recv_ack << " " << lfi_strerror(recv_ack));
                    lfi_client_close(id);
                    return -1;
                }
                debug_info("lfi_send(" << id << ", data.data(), " << std::abs(msg_size) << ")");
                auto send_msg = lfi_send(id, data.data(), std::abs(msg_size));
                if (send_msg < 0) {
                    print("Error lfi_send(" << id << ") = " << send_msg << " " << lfi_strerror(send_msg));
                    lfi_client_close(id);
                    return -1;
                }
            }
            return 0;
        };

        // msg_op();
        tpool->enqueue(msg_op);
    }
}

void signalHandler(int signum) {
    std::cout << "\nSignal (" << signum << ") received." << std::endl;
    tpool.reset();
    std::exit(EXIT_SUCCESS);
}

int main() {
    int server_fd, new_socket;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (std::signal(SIGINT, signalHandler) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
        return 1;
    }

    // Creating socket file descriptor
    int port = PORT;
    if ((server_fd = lfi_server_create(NULL, &port)) < 0) {
        perror("lfi server failed");
        exit(EXIT_FAILURE);
    }

    std::thread(echo_server).detach();

    print("Server start accepting " << LFI::ns::get_host_name() << " :");
    while (true) {
        if ((new_socket = lfi_server_accept(server_fd)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        print("Server accept client " << new_socket);
        clients++;
    }
    // closing the listening socket
    lfi_server_close(server_fd);

    return 0;
}
