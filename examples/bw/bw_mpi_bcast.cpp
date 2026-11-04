
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

#include "bw_common.hpp"
#include "impl/debug.hpp"
#include "mpi.h"

using namespace bw_examples;

static std::vector<uint8_t> data;

int run_test(bw_test &test) {
    ssize_t test_size = test.test_size;
    debug_info("Start run_test size " << test.test_size);
    // std::this_thread::sleep_for(std::chrono::seconds(10));
    MPI_Barrier(MPI_COMM_WORLD);
    timer t;
    for (size_t i = 0; i < test.test_count; i++) {
        auto ret = MPI_Bcast(data.data(), test_size, MPI_UINT8_T, 0, MPI_COMM_WORLD);
        if (ret != MPI_SUCCESS) {
            print("Error MPI_Bcast = " << test_size << " ret " << ret);
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
    std::vector<int> client_fds;

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

    MPI_Barrier(MPI_COMM_WORLD);

    auto &tests = get_test_vector();
    data.resize(tests[tests.size() - 1].test_size);
    print_header();

    for (auto &test : tests) {
        ret = run_test(test);
        if (ret < 0) {
            MPI_Abort(MPI_COMM_WORLD, -1);
            break;
        }
        print_test(test);
    }

    ret = MPI_Finalize();
    if (ret < 0) exit(EXIT_FAILURE);

    return 0;
}
