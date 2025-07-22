
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

#define printf(...) \
    printf("\x1b[32m"); \
    printf(__VA_ARGS__);\
    printf("\x1b[0m");

int main(void) {
    int server_fd, new_socket;
    ssize_t valread, valsend;
    char buffer[1024] = {0};
    char* pong_msg = "Pong!";

    int port = PORT;
    if ((server_fd = lfi_server_create(NULL, &port)) < 0) {
        perror("lfi server failed");
        exit(EXIT_FAILURE);
    }
    printf("Server listening on port %d\n", port);

    if ((new_socket = lfi_server_accept(server_fd)) < 0) {
        perror("lfi server accept");
        exit(EXIT_FAILURE);
    }
    printf("Client connected.\n");

    int count = 0;
    for (int i = 0; i < 5; ++i) {
        memset(buffer, 0, 1024);
        valread = lfi_recv(new_socket, buffer, 1024);
        if (valread <= 0) {
            if (valread == 0) {
                printf("Client disconnected.\n");
            } else {
                printf("lfi_recv failed: %s\n", lfi_strerror(valread));
            }
            break;
        }
        printf("%d: Recv msg size %ld: %s\n", count, valread, buffer);

        if (strcmp(buffer, "Ping!") == 0) {
            valsend = lfi_send(new_socket, pong_msg, strlen(pong_msg));
            if (valsend < 0) {
                printf("lfi_send failed: %s\n", lfi_strerror(valsend));
                break;  // Exit loop on send error
            }
            printf("%d: Sent msg size %ld: %s\n", count, valsend, pong_msg);
        } else {
            printf("Received unexpected message: %s\n", buffer);
            valsend = lfi_send(new_socket, "Unexpected!", strlen("Unexpected!"));
            if (valsend < 0) {
                printf("lfi_send failed for unexpected msg: %s\n", lfi_strerror(valsend));
                break;  // Exit loop on send error
            }
            printf("%d: Sent msg size %ld: Unexpected!\n", count, valsend);
        }
        count++;
    }

    lfi_client_close(new_socket);
    lfi_server_close(server_fd);
    printf("Server closed.\n");
    return 0;
}
