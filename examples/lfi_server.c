// Server side C program to demonstrate Socket
// programming
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
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    if ((new_socket = lfi_server_accept(server_fd)) < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }
    valread = lfi_recv(new_socket, buffer,
                   strlen(hello)); // subtract 1 for the null
                              // terminator at the end
    printf("Recv msg size %ld: %s\n", valread, buffer);
    valsend = lfi_send(new_socket, hello, strlen(hello));
    printf("Hello message sent size %ld\n", valsend);

    // closing the connected socket
    lfi_close(new_socket);
    // closing the listening socket
    lfi_close(server_fd);
    return 0;
}
