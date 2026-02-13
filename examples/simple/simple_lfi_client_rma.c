
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lfi.h"

#define PORT     8081
#define BUF_SIZE 1024

void run_client(const char *server_ip) {
    int client_id;
    uint64_t remote_addr, remote_key;
    char local_buf[BUF_SIZE] = {0};

    sleep(1);  // Wait for server to start

    printf("[CLIENT] Connecting to %s:%d...\n", server_ip, PORT);
    client_id = lfi_client_create(server_ip, PORT);
    if (client_id < 0) {
        printf("[CLIENT] Error creating client\n");
        exit(1);
    }

    lfi_recv(client_id, &remote_addr, sizeof(remote_addr));
    lfi_recv(client_id, &remote_key, sizeof(remote_key));
    printf("[CLIENT] Received remote_addr=%p, remote_key=%lu\n", (void *)remote_addr, remote_key);

    // Test PUT
    const char *msg = "Data put by client";
    printf("[CLIENT] Executing lfi_put...\n");
    if (lfi_put(client_id, msg, strlen(msg) + 1, remote_addr, remote_key) < 0) {
        printf("[CLIENT] lfi_put failed\n");
        exit(1);
    }
    lfi_send(client_id, "D", 1);  // Signal server

    // Test GET
    char ack[2];
    lfi_recv(client_id, ack, 2);
    printf("[CLIENT] Executing lfi_get...\n");
    if (lfi_get(client_id, local_buf, BUF_SIZE, remote_addr, remote_key) < 0) {
        printf("[CLIENT] lfi_get failed\n");
        exit(1);
    }
    printf("[CLIENT] Data got from server: %s\n", local_buf);

    if (strcmp(local_buf, "New data from server") == 0) {
        printf("[CLIENT] RMA verification SUCCESS!\n");
    } else {
        printf("[CLIENT] RMA verification FAILED!\n");
    }

    lfi_send(client_id, "D", 1);
    lfi_client_close(client_id);
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        run_client(argv[1]);
    } else {
        run_client("127.0.0.1");
    }
    return 0;
}
