#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <liburing.h>
#include <string.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <archivo>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct io_uring ring;
    if (io_uring_queue_init(4, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        return 1;
    }

    char *buffer = (char*)malloc(BUFFER_SIZE);
    if (!buffer) {
        perror("malloc");
        return 1;
    }

    off_t offset = 0;
    int bytes_read;
    iovec vec = {};
    vec.iov_base = buffer;
    vec.iov_len = BUFFER_SIZE;

    while (1) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_readv(sqe, fd, &vec, 1, offset);

        struct io_uring_cqe *cqe;
        io_uring_submit(&ring);
        io_uring_wait_cqe(&ring, &cqe);

        bytes_read = cqe->res;
        if (bytes_read > 0) {
            write(STDOUT_FILENO, buffer, bytes_read);
            offset += bytes_read;
        } else if (bytes_read == 0) {
            // Fin del archivo
            break;
        } else {
            fprintf(stderr, "Error en la lectura: %s\n", strerror(-bytes_read));
            break;
        }

        io_uring_cqe_seen(&ring, cqe);
    }

    io_uring_queue_exit(&ring);
    close(fd);
    free(buffer);

    return 0;
}