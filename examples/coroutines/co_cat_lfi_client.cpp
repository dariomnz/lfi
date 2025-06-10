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
#include <vector>

#include "lfi.h"
#define PORT 8080

int main(int argc, char* argv[]) {
    int client_fd;
    ssize_t valread, valsend;
    std::vector<char> buffer(1024, '\0');

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
        std::cout << "Write the path to cat: \n";
        std::cin >> input;
        valsend = lfi_send(client_fd, input.data(), input.size());
        std::cout << "Message sent size " << valsend << std::endl;

        ssize_t buff_size = 0;
        std::cout << "Content of file " << input << std::endl;
        std::cout << "--------------------------------" << std::endl;
        while (true) {
            valread = lfi_recv(client_fd, &buff_size, sizeof(buff_size));
            if (valread < 0) {
                std::cerr << "Error in lfi_recv " << valread << " " << lfi_strerror(valread);
                break;
            }
            if (buff_size < 0) {
                std::cout << "Error in server" << std::endl;
                break;
            }
            if (buff_size == 0) {
                std::cout << "--------------------------------" << std::endl;
                break;
            }

            valread = lfi_recv(client_fd, buffer.data(), buff_size);
            if (valread < 0) {
                std::cerr << "Error in lfi_recv " << valread << " " << lfi_strerror(valread);
                break;
            }
            std::cout << std::string_view(buffer.data(), buff_size);
        }
    }

    lfi_client_close(client_fd);
    return 0;
}
