
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
#include "lfi.h"
#define PORT 8080
int main(void)
{
    int server_fd, new_socket;
    ssize_t valread, valsend;
    char buffer[1024] = { 0 };
    char* hello = "Hello from server";

    // Creating socket file descriptor
    if ((server_fd = lfi_server_create(NULL, PORT)) < 0) {
        perror("lfi server failed");
        exit(EXIT_FAILURE);
    }
    if ((new_socket = lfi_server_accept(server_fd)) < 0) {
        perror("lfi server accept");
        exit(EXIT_FAILURE);
    }
    valread = lfi_recv(new_socket, buffer,
                   strlen(hello)); // subtract 1 for the null
                              // terminator at the end
    printf("Recv msg size %ld: %s\n", valread, buffer);
    valsend = lfi_send(new_socket, hello, strlen(hello));
    printf("Hello message sent size %ld\n", valsend);

    // closing the connected socket
    lfi_client_close(new_socket);
    // closing the listening socket
    lfi_server_close(server_fd);
    return 0;
}
