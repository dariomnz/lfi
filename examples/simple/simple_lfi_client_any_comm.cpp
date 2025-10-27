
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

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <thread>

#include "debug.hpp"
#include "lfi.h"
#define PORT 8080

int main(int argc, char *argv[]) {
    int client_fd;
    ssize_t valread, valsend;

    if (argc < 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return -1;
    }

    if ((client_fd = lfi_client_create(argv[1], PORT)) < 0) {
        printf("lfi client creation error \n");
        return -1;
    }

    print("Sleep 5 sec");
    std::this_thread::sleep_for(std::chrono::seconds(10));

    srand(time(NULL));

    int32_t sended = rand();
    int32_t received = 0;
    print("Client send: " << sended);
    valsend = lfi_send(client_fd, &sended, sizeof(sended));
    if (valsend < 0) {
        print("Error: lfi_send " << lfi_strerror(valsend));
    }

    print("Sleep 5 sec");
    std::this_thread::sleep_for(std::chrono::seconds(10));

    valread = lfi_recv(client_fd, &received, sizeof(received));
    if (valread < 0) {
        print("Error: lfi_recv " << lfi_strerror(valread));
    }
    print("Client recv: " << received << " sended: " << sended);

    // closing the connected socket
    lfi_client_close(client_fd);
    return 0;
}
