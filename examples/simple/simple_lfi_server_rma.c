
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lfi.h"

#define PORT     8081
#define BUF_SIZE 1024

void run_server() {
    int server_id, client_id;
    int port = PORT;
    char buffer[BUF_SIZE] = "Original server data";
    uint64_t key;

    printf("[SERVER] Creating server on port %d...\n", port);
    server_id = lfi_server_create(NULL, &port);
    if (server_id < 0) {
        perror("lfi_server_create");
        exit(1);
    }

    printf("[SERVER] Registering memory...\n");
    key = lfi_mr_reg(buffer, BUF_SIZE);
    if (key < 0) {
        printf("[SERVER] Error registering memory\n");
        exit(1);
    }

    printf("[SERVER] Waiting for client...\n");
    client_id = lfi_server_accept(server_id);
    if (client_id < 0) {
        printf("[SERVER] Error accepting connection\n");
        exit(1);
    }

    // Send address and key to client
    uint64_t addr = (uint64_t)buffer;
    printf("[SERVER] Sending addr=%p and key=%lu to client\n", buffer, key);
    lfi_send(client_id, &addr, sizeof(addr));
    lfi_send(client_id, &key, sizeof(key));

    // Wait for client to signal completion
    char signal;
    lfi_recv(client_id, &signal, 1);

    printf("[SERVER] Data after client PUT: %s\n", buffer);

    // Change data for client GET
    strcpy(buffer, "New data from server");
    printf("[SERVER] Data changed for client GET: %s\n", buffer);
    lfi_send(client_id, "OK", 2);

    lfi_recv(client_id, &signal, 1);

    lfi_mr_unreg(key);
    lfi_client_close(client_id);
    lfi_server_close(server_id);
}

int main() {
    run_server();
    return 0;
}
