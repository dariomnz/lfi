#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "lfi.h"

typedef struct {
    int client_id;
    uint64_t remote_addr;
    lfi_mr_key remote_key;
    size_t size;
} lfi_file_t;

int lfi_file_open(const char *server_ip, const char *filename, lfi_file_t *file) {
    int client_id = lfi_client_create(server_ip, PORT);
    if (client_id < 0) {
        return client_id;
    }

    struct request req;
    struct response res;

    memset(&req, 0, sizeof(req));
    req.op = OP_OPEN;
    strncpy(req.filename, filename, sizeof(req.filename));

    if (lfi_send(client_id, &req, sizeof(req)) < 0) {
        lfi_client_close(client_id);
        return -1;
    }

    if (lfi_recv(client_id, &res, sizeof(res)) < 0) {
        lfi_client_close(client_id);
        return -1;
    }

    if (res.status != 0) {
        lfi_client_close(client_id);
        return -1;
    }

    file->client_id = client_id;
    file->remote_addr = res.remote_addr;
    file->remote_key = res.remote_key;
    file->size = res.size;

    return 0;
}

int lfi_file_resize(lfi_file_t *file, size_t new_size) {
    struct request req;
    struct response res;

    memset(&req, 0, sizeof(req));
    req.op = OP_RESIZE;
    req.size = new_size;

    if (lfi_send(file->client_id, &req, sizeof(req)) < 0) return -1;
    if (lfi_recv(file->client_id, &res, sizeof(res)) < 0) return -1;

    if (res.status == 0) {
        file->remote_addr = res.remote_addr;
        file->remote_key = res.remote_key;
        file->size = res.size;
        return 0;
    }
    return -1;
}

ssize_t lfi_file_read(lfi_file_t *file, void *buf, size_t count, off_t offset) {
    if (offset + count > file->size) {
        return -1;
    }
    return lfi_get(file->client_id, buf, count, file->remote_addr + offset, file->remote_key);
}

ssize_t lfi_file_write(lfi_file_t *file, const void *buf, size_t count, off_t offset) {
    if (offset + count > file->size) {
        printf("[CLIENT] Auto-resizing file to %zu bytes\n", (size_t)(offset + count));
        if (lfi_file_resize(file, offset + count) < 0) {
            return -1;
        }
    }
    return lfi_put(file->client_id, buf, count, file->remote_addr + offset, file->remote_key);
}

int lfi_file_close(lfi_file_t *file) {
    if (file->client_id < 0) return 0;
    struct request req;
    struct response res;

    memset(&req, 0, sizeof(req));
    req.op = OP_CLOSE;
    lfi_send(file->client_id, &req, sizeof(req));
    lfi_recv(file->client_id, &res, sizeof(res));

    int ret = lfi_client_close(file->client_id);
    file->client_id = -1;
    return ret;
}

void run_client(const char *server_ip) {
    lfi_file_t file;

    printf("[CLIENT] Connecting to %s:%d...\n", server_ip, PORT);
    if (lfi_file_open(server_ip, "lfi_large_test.bin", &file) < 0) {
        fprintf(stderr, "[CLIENT] Failed to open file\n");
        exit(1);
    }

    printf("[CLIENT] File opened. Size: %zu\n", file.size);

    size_t total_size = 1024 * 1024;  // 1MB
    size_t block_size = 64 * 1024;    // 64KB
    char *large_data = malloc(block_size);
    for (size_t i = 0; i < block_size; i++) {
        if (i % 5 == 0) {
            large_data[i] = '\n';
        } else {
            large_data[i] = 'A';
        }
    }

    printf("[CLIENT] Writing 1MB in %zu blocks...\n", total_size / block_size);
    for (size_t i = 0; i < total_size; i += block_size) {
        ssize_t written = lfi_file_write(&file, large_data, block_size, i);
        if (written < 0) {
            fprintf(stderr, "[CLIENT] Error writing at offset %zu\n", i);
            break;
        }
    }
    printf("[CLIENT] Write complete. New file size: %zu\n", file.size);

    char verify[10];
    lfi_file_read(&file, verify, 10, total_size - 10);
    printf("[CLIENT] Verification (last 10 bytes): %.10s\n", verify);

    free(large_data);
    lfi_file_close(&file);
    printf("[CLIENT] Done.\n");
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        run_client(argv[1]);
    } else {
        run_client("127.0.0.1");
    }
    return 0;
}
