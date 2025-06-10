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

#include <iostream>
#include <string>

#include "lfi.h"
#define PORT 8080

int main(int argc, char* argv[]) {
    int client_fd;
    ssize_t valread, valsend;
    constexpr size_t buff_size = 1024;
    char buffer[buff_size] = {};

    if (argc < 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return -1;
    }

    if ((client_fd = lfi_client_create(argv[1], PORT)) < 0) {
        printf("lfi client creation error \n");
        return -1;
    }

    while (true) {
        std::string input;
        std::cout << "Write a message: " << std::endl;
        std::cin >> input;
        valsend = lfi_send(client_fd, input.data(), input.size());
        std::cout << "Message sent size " << valsend << std::endl;

        valread = lfi_recv(client_fd, buffer, buff_size);
        std::cout << "Recv msg size " << valread << ": " << std::string_view(buffer, valread) << std::endl;
    }

    lfi_client_close(client_fd);
    return 0;
}
