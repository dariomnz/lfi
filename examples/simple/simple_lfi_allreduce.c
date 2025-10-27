
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

#include "lfi.h"
#include "lfi_coll.h"
#define PORT 8080

void print_current_timestamp() {
    struct timeval tv;
    struct tm *tm_info;
    gettimeofday(&tv, NULL);
    tm_info = localtime(&tv.tv_sec);
    printf("%02d:%02d:%02d:%03ld ", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, tv.tv_usec / 1000);
}

int split(const char *txt, char delim, char ***tokens) {
    int *tklen, *t, count = 1;
    char **arr, *p = (char *)txt;

    while (*p != '\0')
        if (*p++ == delim) count += 1;
    t = tklen = (int *)calloc(count, sizeof(int));
    for (p = (char *)txt; *p != '\0'; p++) *p == delim ? *t++ : (*t)++;
    *tokens = arr = (char **)malloc(count * sizeof(char *));
    t = tklen;
    p = *arr++ = (char *)calloc(*(t++) + 1, sizeof(char *));
    while (*txt != '\0') {
        if (*txt == delim) {
            p = *arr++ = (char *)calloc(*(t++) + 1, sizeof(char *));
            txt++;
        } else
            *p++ = *txt++;
    }
    free(tklen);
    return count;
}

#define print(...)                 \
    {                              \
        print_current_timestamp(); \
        printf(__VA_ARGS__);       \
        fflush(stdout);            \
    }

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print("Usage: %s <servers_ip comma separated>\n", argv[0]);
        return -1;
    }
    char **tokens;
    int count, i;
    count = split(argv[1], ',', &tokens);
    // for (i = 0; i < count; i++) print("%s\n", tokens[i]);

    lfi_group group;
    int ret = lfi_group_create((const char **)tokens, count, &group);
    if (ret < 0) {
        print("Error lfi_group_create: %s\n", lfi_strerror(ret));
        exit(EXIT_FAILURE);
    }

    int rank, size;
    lfi_group_rank(&group, &rank);
    lfi_group_size(&group, &size);

    if (rank == 0) {
        print("Sleep\n");
        sleep(2);
    }
    print("Before barrier in %d\n", rank);
    double start = lfi_time(&group);
    lfi_barrier(&group);
    double end = lfi_time(&group);
    print("After barrier in %d in %f\n", rank, (end - start));

    int value_min = rank;
    print("Before lfi_allreduce LFI_OP_MIN %d in %d\n", value_min, rank);
    double start_min = lfi_time(&group);
    lfi_allreduce(&group, &value_min, LFI_OP_TYPE_INT, LFI_OP_MIN);
    double end_min = lfi_time(&group);
    print("After lfi_allreduce LFI_OP_MIN %d in %d in %f\n", value_min, rank, (end_min - start_min));

    int value_max = rank;
    print("Before lfi_allreduce LFI_OP_MAX %d in %d\n", value_max, rank);
    double start_max = lfi_time(&group);
    lfi_allreduce(&group, &value_max, LFI_OP_TYPE_INT, LFI_OP_MAX);
    double end_max = lfi_time(&group);
    print("After lfi_allreduce LFI_OP_MAX %d in %d in %f\n", value_max, rank, (end_max - start_max));

    int value_sum = rank;
    print("Before lfi_allreduce LFI_OP_SUM %d in %d\n", value_sum, rank);
    double start_sum = lfi_time(&group);
    lfi_allreduce(&group, &value_sum, LFI_OP_TYPE_INT, LFI_OP_SUM);
    double end_sum = lfi_time(&group);
    print("After lfi_allreduce LFI_OP_SUM %d in %d in %f\n", value_sum, rank, (end_sum - start_sum));

    int value_prod = rank;
    print("Before lfi_allreduce LFI_OP_PROD %d in %d\n", value_prod, rank);
    double start_prod = lfi_time(&group);
    lfi_allreduce(&group, &value_prod, LFI_OP_TYPE_INT, LFI_OP_PROD);
    double end_prod = lfi_time(&group);
    print("After lfi_allreduce LFI_OP_PROD %d in %d in %f\n", value_prod, rank, (end_prod - start_prod));

    ret = lfi_group_close(&group);
    if (ret < 0) {
        print("Error lfi_group_close: %s\n", lfi_strerror(ret));
    }

    for (i = 0; i < count; i++) free(tokens[i]);
    free(tokens);
}
