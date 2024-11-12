
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
#include "lfi.h"
#define PORT 8080

int main(int argc, char *argv[])
{
    int client_fd;
    ssize_t valread, valsend;
    char* hello = "Hello from client";
    char buffer[1024] = { 0 };
    
    if (argc < 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return -1;
    }

    if ((client_fd = lfi_client_create(argv[1], PORT)) < 0) {
        printf("lfi client creation error \n");
        return -1;
    }

    valsend = lfi_send(client_fd, hello, strlen(hello));
    printf("Hello message sent size %ld\n", valsend);
    valread = lfi_recv(client_fd, buffer,
                   strlen(hello)); // subtract 1 for the null
                              // terminator at the end
    printf("Recv msg size %ld: %s\n", valread, buffer);

    // closing the connected socket
    lfi_client_close(client_fd);
    return 0;
}
