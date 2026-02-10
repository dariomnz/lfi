
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

#include "echo_common.hpp"
#include "impl/debug.hpp"
#include "impl/ns.hpp"
#include "lfi.h"
#include "lfi_async.h"
#include "thread_pool.hpp"

using namespace bw_examples;

#define MAX_MSG_SIZE 4 * 1024 * 1024  // 4 Mb
#define TAG_MSG      100
#define TAG_DATA     101
#define TAG_ACK      102

static std::atomic<int> clients = 0;

#define LFI_TAG_DUMMY (0xFFFFFFFF - 100)

std::unique_ptr<lfi_request, void (*)(lfi_request *)> trigger_request(lfi_request_create(LFI_ANY_COMM_SHM),
                                                                      lfi_request_free);

void post_recv(lfi_request *req) {
    static std::vector<uint8_t> data(MAX_MSG_SIZE);
    debug_info("lfi_trecv_async");
    auto ret = lfi_trecv_async(req, data.data(), data.size(), TAG_DATA);
    if (ret < 0) {
        print("Error lfi_trecv_async " << lfi_strerror(ret));
        exit(EXIT_FAILURE);
    }
}

void send_callback(int error, void *ctx) {
    lfi_request *req = static_cast<lfi_request *>(ctx);

    if (error < 0) {
        print("Error recv_callback " << lfi_strerror(error));
    }
    debug_info("lfi_request_free");
    lfi_request_free(req);
}

void send_ack(int comm_id) {
    static int dummy = 0;
    lfi_request *req = lfi_request_create(comm_id);
    lfi_request_set_callback(req, send_callback, req);
    debug_info("lfi_tsend_async " << comm_id);
    auto ret = lfi_tsend_async(req, &dummy, sizeof(dummy), TAG_ACK);
    if (ret < 0) {
        print("Error lfi_trecv_async " << lfi_strerror(ret));
        exit(EXIT_FAILURE);
    }
}

void recv_callback(int error, void *ctx) {
    lfi_request *req = static_cast<lfi_request *>(ctx);

    if (error < 0) {
        print("Error recv_callback " << lfi_strerror(error));
        lfi_request_free(req);

        lfi_client_close(lfi_request_source(req));
    } else {
        send_ack(lfi_request_source(req));
        post_recv(req);
    }
}

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
    if (lfi_trecv_async(trigger_request.get(), nullptr, 0, LFI_TAG_DUMMY) < 0) {
        print("Error in lfi_trecv_async");
        return;
    }
    while (true) {
        debug_info("Start recv any ack");

        lfi_request *requests[3] = {shm_request.get(), peer_request.get(), trigger_request.get()};

        int completed = lfi_wait_any(requests, 3);
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
        } else if (completed == 2) {
            print("Trigger finish");
            break;
        } else {
            print("Error in wait_num");
            break;
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
    }
}

int server_fd;
void signalHandler(int signum) {
    std::cout << "\nSignal (" << signum << ") received." << std::endl;
    lfi_server_close(server_fd);
}

int main() {
    int new_socket;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (std::signal(SIGINT, signalHandler) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
        return 1;
    }

    // Creating socket file descriptor
    int port = PORT_LFI;
    if ((server_fd = lfi_server_create(NULL, &port)) < 0) {
        perror("lfi server failed");
        exit(EXIT_FAILURE);
    }

    std::thread echo_thread(echo_server);

    print("Server start accepting " << LFI::ns::get_host_name() << " :");
    while (true) {
        if ((new_socket = lfi_server_accept(server_fd)) < 0) {
            perror("accept");
            break;
        }
        print("Server accept client " << new_socket);
        auto req = lfi_request_create(new_socket);
        lfi_request_set_callback(req, recv_callback, req);
        post_recv(req);
        clients++;
    }

    lfi_cancel(trigger_request.get());
    echo_thread.join();

    // closing the listening socket
    lfi_server_close(server_fd);

    return 0;
}
