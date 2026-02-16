#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "lfi.h"

void run_server() {
    int server_id, client_id;
    int port = PORT;

    printf("[SERVER] Starting file server on port %d...\n", port);
    server_id = lfi_server_create(NULL, &port);
    if (server_id < 0) {
        fprintf(stderr, "[SERVER] Failed to create server: %s\n", lfi_strerror(server_id));
        exit(1);
    }

    while (1) {
        printf("[SERVER] Waiting for client connection...\n");
        client_id = lfi_server_accept(server_id);
        if (client_id < 0) {
            fprintf(stderr, "[SERVER] Failed to accept connection\n");
            continue;
        }

        printf("[SERVER] Client connected (ID: %d)\n", client_id);

        struct request req;
        struct response res;
        void *mapped_addr = NULL;
        size_t mapped_size = 0;
        int fd = -1;
        lfi_mr_key key;

        while (lfi_recv(client_id, &req, sizeof(req)) > 0) {
            memset(&res, 0, sizeof(res));
            if (req.op == OP_OPEN) {
                printf("[SERVER] Received OPEN for '%s'\n", req.filename);
                fd = open(req.filename, O_RDWR | O_CREAT, 0666);
                if (fd < 0) {
                    perror("open");
                    res.status = -1;
                } else {
                    struct stat st;
                    fstat(fd, &st);
                    // For demonstration, ensure the file has a minimum size
                    if (st.st_size < 4096) {
                        st.st_size = 4096;
                        if (ftruncate(fd, st.st_size) < 0) {
                            perror("ftruncate");
                        }
                    }
                    mapped_size = st.st_size;
                    mapped_addr = mmap(NULL, mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                    if (mapped_addr == MAP_FAILED) {
                        perror("mmap");
                        res.status = -1;
                        close(fd);
                        fd = -1;
                    } else {
                        key = lfi_mr_reg(mapped_addr, mapped_size);
                        if (key.shm_key < 0) {
                            fprintf(stderr, "[SERVER] Failed to register memory: %s\n", lfi_strerror(key.shm_key));
                            munmap(mapped_addr, mapped_size);
                            close(fd);
                            fd = -1;
                            res.status = -1;
                        } else {
                            res.remote_addr = (uint64_t)mapped_addr;
                            res.remote_key = key;
                            res.size = mapped_size;
                            res.status = 0;
                            printf("[SERVER] File opened, mapped at %p, key %ld-%ld, size %zu\n", mapped_addr,
                                   key.shm_key, key.peer_key, mapped_size);
                        }
                    }
                }
                lfi_send(client_id, &res, sizeof(res));
            } else if (req.op == OP_RESIZE) {
                printf("[SERVER] Received RESIZE to %zu bytes\n", req.size);
                if (fd < 0) {
                    res.status = -1;
                } else {
                    // Update file size
                    if (ftruncate(fd, req.size) < 0) {
                        perror("ftruncate");
                        res.status = -1;
                    } else {
                        // Unregister and unmap old region
                        if (key.shm_key >= 0) lfi_mr_unreg(key);
                        if (mapped_addr && mapped_addr != MAP_FAILED) munmap(mapped_addr, mapped_size);

                        // Map and register new region
                        mapped_size = req.size;
                        mapped_addr = mmap(NULL, mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                        if (mapped_addr == MAP_FAILED) {
                            perror("mmap");
                            res.status = -1;
                            close(fd);
                            fd = -1;
                        } else {
                            key = lfi_mr_reg(mapped_addr, mapped_size);
                            if (key.shm_key < 0) {
                                res.status = -1;
                                munmap(mapped_addr, mapped_size);
                                close(fd);
                                fd = -1;
                            } else {
                                res.remote_addr = (uint64_t)mapped_addr;
                                res.remote_key = key;
                                res.size = mapped_size;
                                res.status = 0;
                            }
                        }
                    }
                }
                lfi_send(client_id, &res, sizeof(res));
            } else if (req.op == OP_CLOSE) {
                printf("[SERVER] Received CLOSE\n");
                if (key.shm_key >= 0) {
                    lfi_mr_unreg(key);
                    key.shm_key = -1;
                    key.peer_key = -1;
                }
                if (mapped_addr && mapped_addr != MAP_FAILED) {
                    munmap(mapped_addr, mapped_size);
                    mapped_addr = NULL;
                }
                if (fd >= 0) {
                    close(fd);
                    fd = -1;
                }
                res.status = 0;
                lfi_send(client_id, &res, sizeof(res));
                break;  // Exit command loop for this client
            } else {
                printf("[SERVER] Unknown operation: %d\n", req.op);
                res.status = -1;
                lfi_send(client_id, &res, sizeof(res));
            }
        }

        printf("[SERVER] Closing client connection (ID: %d)\n", client_id);
        lfi_client_close(client_id);
    }

    lfi_server_close(server_id);
}

int main() {
    run_server();
    return 0;
}
