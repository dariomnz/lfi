// Client side C program to demonstrate Socket
// programming
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "lfi.h"
#define PORT 8080

int main(void)
{
    int client_fd;
    ssize_t valread, valsend;
    char* hello = "Hello from client";
    char buffer[1024] = { 0 };
    if ((client_fd = lfi_client_create("127.0.0.1", PORT)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    valsend = lfi_send(client_fd, hello, strlen(hello));
    printf("Hello message sent size %ld\n", valsend);
    valread = lfi_recv(client_fd, buffer,
                   strlen(hello)); // subtract 1 for the null
                              // terminator at the end
    printf("Recv msg size %ld: %s\n", valread, buffer);

    // closing the connected socket
    lfi_close(client_fd);
    return 0;
}
