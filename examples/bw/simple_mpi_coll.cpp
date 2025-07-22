
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
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "mpi.h"

void print_current_timestamp() {
    struct timeval tv;
    struct tm *tm_info;
    gettimeofday(&tv, NULL);
    tm_info = localtime(&tv.tv_sec);
    printf("%02d:%02d:%02d:%03ld ", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, tv.tv_usec / 1000);
}

#define print(...)                 \
    {                              \
        print_current_timestamp(); \
        printf(__VA_ARGS__);       \
        fflush(stdout);            \
    }

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (rank == 0) {
        print("Sleep\n");
        sleep(2);
    }
    print("Before barrier in %d\n", rank);
    double start = MPI_Wtime();
    MPI_Barrier(MPI_COMM_WORLD);
    double end = MPI_Wtime();
    print("After barrier in %d in %f\n", rank, (end - start));

    if (rank == 0) {
        print("Sleep\n");
        fflush(stdin);
        sleep(2);
    }
    char msg[1024];
    memset(msg, 0, 1024);
    sprintf(msg, "Broadcast msg rank %d", rank);
    print("Before broadcast '%s' in %d\n", msg, rank);
    double start_cast = MPI_Wtime();
    MPI_Bcast(msg, sizeof(msg), MPI_BYTE, 0, MPI_COMM_WORLD);
    double end_cast = MPI_Wtime();
    print("After broadcast '%s' in %d in %f\n", msg, rank, (end_cast - start_cast));

    size_t buffer_size = 1024 * 1024 * 1024;
    char *buffer = (char *)malloc(buffer_size);
    print("Before broadcast in %d\n", rank);
    double start_cast2 = MPI_Wtime();
    MPI_Bcast(buffer, buffer_size, MPI_BYTE, 0, MPI_COMM_WORLD);
    double end_cast2 = MPI_Wtime();
    print("After broadcast in %d in %f\n", rank, (end_cast2 - start_cast2));
    free(buffer);

    MPI_Finalize();
}
