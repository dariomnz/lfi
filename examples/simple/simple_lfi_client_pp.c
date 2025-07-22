
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

#define printf(...) \
    printf("\x1b[34m"); \
    printf(__VA_ARGS__);\
    printf("\x1b[0m");

int main(int argc, char* argv[]) {
    int client_fd;
    ssize_t valread, valsend;
    char* ping_msg = "Ping!";
    char buffer[1024] = {0};

    if (argc < 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return -1;
    }

    if ((client_fd = lfi_client_create(argv[1], PORT)) < 0) {
        printf("lfi client creation error \n");
        return -1;
    }
    printf("Connected to server at %s:%d\n", argv[1], PORT);

    for (int i = 0; i < 5; ++i) {
        valsend = lfi_send(client_fd, ping_msg, strlen(ping_msg));
        if (valsend < 0) {
            printf("lfi_send failed: %s\n", lfi_strerror(valsend));
            break;  // Exit loop on send error
        }
        printf("%d: Sent msg size %ld: %s\n", i, valsend, ping_msg);

        // sleep(20);
        memset(buffer, 0, 1024);
        valread = lfi_recv(client_fd, buffer, 1024);
        if (valread <= 0) {
            if (valread == 0) {
                printf("Server disconnected.\n");
            } else {
                printf("lfi_recv failed: %s\n", lfi_strerror(valread));
            }
            break;
        }
        printf("%d: Recv msg size %ld: %s\n", i, valread, buffer);

        if (strcmp(buffer, "Pong!") != 0) {
            printf("Received unexpected message from server: %s\n", buffer);
        }

        sleep(1);
    }

    lfi_client_close(client_fd);
    printf("Client closed.\n");
    return 0;
}