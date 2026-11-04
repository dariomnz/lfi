
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
// #define DEBUG
#include "bw_common.hpp"
#include "impl/debug.hpp"
#include "lfi.h"
#include "lfi_coll.h"
#include "lfi_error.h"
#include "mpi.h"

using namespace bw_examples;

static std::vector<uint8_t> data;

int run_test(lfi_group *group, bw_test &test) {
    ssize_t test_size = test.test_size;
    debug_info("Start run_test size " << test.test_size);
    MPI_Barrier(MPI_COMM_WORLD);
    timer t;
    for (size_t i = 0; i < test.test_count; i++) {
        auto ret = lfi_broadcast(group, 0, data.data(), test_size);
        if (ret != LFI_SUCCESS) {
            print("Error MPI_Bcast = " << test_size << " ret " << ret << " " << lfi_strerror(ret));
            return -1;
        }
        test.size += test_size;
    }

    MPI_Barrier(MPI_COMM_WORLD);
    test.nanosec += t.resetElapsedNano();

    debug_info("End run_test size " << test.test_size);

    return 0;
}

int main(int argc, char *argv[]) {
    int ret;

    if (argc < 1) {
        printf("Usage: %s\n", argv[0]);
        return -1;
    }

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    ret = MPI_Init(&argc, &argv);
    if (ret < 0) exit(EXIT_FAILURE);

    int rank;
    ret = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (ret < 0) exit(EXIT_FAILURE);
    int size;
    ret = MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (ret < 0) exit(EXIT_FAILURE);
    std::vector<std::string> servers;
    servers.reserve(size);
    for (int i = 0; i < size; i++) {
        char name[MPI_MAX_PROCESSOR_NAME] = {};
        int nameLen = MPI_MAX_PROCESSOR_NAME;
        ret = MPI_Get_processor_name(name, &nameLen);
        if (ret != MPI_SUCCESS) {
            print("Error MPI_Get_processor_name " << ret);
            MPI_Abort(MPI_COMM_WORLD, -1);
            return -1;
        }
        ret = MPI_Bcast(name, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, i, MPI_COMM_WORLD);
        if (ret != MPI_SUCCESS) {
            print("Error MPI_Get_processor_name " << ret);
            MPI_Abort(MPI_COMM_WORLD, -1);
            return -1;
        }

        if (rank == i) {
            print("Rank " << rank << ": " << name);
        }
        servers.emplace_back(name);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    timer t;

    std::vector<const char *> hostnames;
    for (auto &&srv : servers) {
        hostnames.emplace_back(srv.c_str());
    }
    lfi_group group = {};

    ret = lfi_group_create(hostnames.data(), hostnames.size(), &group);
    if (ret != LFI_SUCCESS) {
        print("Error lfi_group_create " << ret << " " << lfi_strerror(ret));
        MPI_Abort(MPI_COMM_WORLD, -1);
        return -1;
    }

    debug_info("lfi_group_create " << lfi_strerror(ret));
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) {
        print("Connection time to " << servers.size() << " servers: " << (t.resetElapsedNano() * 0.000'001) << " ms");
    }

    MPI_Barrier(MPI_COMM_WORLD);

    auto &tests = get_test_vector();
    data.resize(tests[tests.size() - 1].test_size);
    print_header();

    for (auto &test : tests) {
        ret = run_test(&group, test);
        if (ret < 0) {
            MPI_Abort(MPI_COMM_WORLD, -1);
            break;
        }
        print_test(test);
    }

    lfi_group_close(&group);

    ret = MPI_Finalize();
    if (ret < 0) exit(EXIT_FAILURE);

    return 0;
}
