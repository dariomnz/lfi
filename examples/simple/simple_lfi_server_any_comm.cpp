
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

#include "impl/debug.hpp"
#include "impl/ns.hpp"
#include "lfi.h"
#include "lfi_async.h"
#include "lfi_error.h"

#define PORT 8080
int main() {
    const int max_clients = 1024;
    int server_fd;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    // Creating socket file descriptor
    int port = PORT;
    if ((server_fd = lfi_server_create(NULL, &port)) < 0) {
        perror("lfi server failed");
        exit(EXIT_FAILURE);
    }

    std::cout << "Server start accepting " << LFI::ns::get_host_name() << " :" << std::endl;
    std::thread echo_thread([]() {
        int iter = 0;
        int32_t ack_shm = 0;
        int32_t ack_peer = 0;

        std::unique_ptr<lfi_request, void (*)(lfi_request *)> shm_request(lfi_request_create(LFI_ANY_COMM_SHM),
                                                                          lfi_request_free);
        shm_request.reset();
        std::unique_ptr<lfi_request, void (*)(lfi_request *)> peer_request(lfi_request_create(LFI_ANY_COMM_PEER),
                                                                           lfi_request_free);
        peer_request.reset();
        while (iter < max_clients) {
            print("Start recv any ack");
            int ret = 0;
            int source = -1;
            int error = 0;
            int32_t ack = 0;

            if (!shm_request) {
                shm_request = {lfi_request_create(LFI_ANY_COMM_SHM), lfi_request_free};
                if (!shm_request) {
                    print("Error shm_request is null");
                }

                ret = lfi_recv_async(shm_request.get(), &ack_shm, sizeof(ack_shm));
                if (ret < 0) {
                    print("Error in lfi_trecv_async " << lfi_strerror(ret));
                    return -1;
                }
            }

            if (!peer_request) {
                peer_request = {lfi_request_create(LFI_ANY_COMM_PEER), lfi_request_free};
                if (!peer_request) {
                    print("Error peer_request is null");
                }

                ret = lfi_recv_async(peer_request.get(), &ack_peer, sizeof(ack_peer));
                if (ret < 0) {
                    print("Error in lfi_trecv_async " << lfi_strerror(ret));
                    return -1;
                }
            }

            lfi_request *requests[2] = {shm_request.get(), peer_request.get()};

            int completed = lfi_wait_any(requests, 2);

            std::cout << "Completed wait_num with " << completed << std::endl;
            if (completed == 0) {
                source = lfi_request_source(shm_request.get());
                error = lfi_request_error(shm_request.get());
                ack = ack_shm;
                shm_request.reset();
            } else if (completed == 1) {
                source = lfi_request_source(peer_request.get());
                error = lfi_request_error(peer_request.get());
                ack = ack_peer;
                peer_request.reset();
            } else {
                print("Error in wait_num ");
                exit(1);
            }

            print("Received from " << source << " " << lfi_strerror(error));

            if (error == LFI_SUCCESS) {
                int32_t response = ack + 10;
                print("Send to " << source << " " << response);
                int ret = lfi_send(source, &response, sizeof(response));
                if (ret < 0) {
                    print("Error: lfi_send " << lfi_strerror(ret));
                }
            }
            lfi_client_close(source);

            iter++;
        }
        return 0;
    });
    echo_thread.detach();

    int iter = 0;
    int new_socket;
    while (iter < max_clients) {
        if ((new_socket = lfi_server_accept(server_fd)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        std::cout << "Server accept client " << new_socket << std::endl;
        iter++;
    }

    // closing the listening socket
    lfi_server_close(server_fd);

    return 0;
}
