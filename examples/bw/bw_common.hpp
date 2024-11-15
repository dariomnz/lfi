
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

#pragma once

#include <atomic>
#include <chrono>
#include <vector>
#include <iostream>
#include <iomanip>

namespace bw_examples
{
    constexpr const int PORT = 8080;

    constexpr const uint64_t KB = 1024;
    constexpr const uint64_t MB = KB * 1024;
    constexpr const uint64_t GB = MB * 1024;

    struct bw_test
    {
        std::atomic_uint64_t test_size = 0;
        std::atomic_uint64_t test_count = 0;

        std::atomic_uint64_t size = 0;
        std::atomic_uint64_t nanosec = 0;
    };

    static std::vector<bw_test> &get_test_vector()
    {
        static std::vector<bw_test> tests(25);
        static bool initialized = false;
        if (!initialized)
        {
            initialized = true;

            for (size_t i = 0; i < tests.size(); i++)
            {
                tests[i].test_size = 1ull << i;
                tests[i].test_count = 10;
            }
        }
        return tests;
    }

    [[maybe_unused]] static void print_header()
    {

        int rank;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (rank == 0)
        {
            std::cout << std::left
                      << std::setw(22) << "Size (bytes)" << " | "
                      << std::setw(06) << "Count" << " | "
                      << std::setw(22) << "Latency (ms)" << " | "
                      << std::setw(22) << "Bandwidth (MB/s)" << " | "
                      << std::endl;
        }
    }

    [[maybe_unused]] static void print_test(bw_test &test)
    {
        uint64_t s_test_count = test.test_count*2;
        uint64_t s_nanosec = test.nanosec;
        double s_bw = ((double)test.size / (double)MB) / ((double)test.nanosec / 1'000'000'000.0);

        uint64_t r_test_count = 0;
        uint64_t r_nanosec = 0;
        double r_bw = 0;

        MPI_Reduce(&s_bw, &r_bw, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&s_test_count, &r_test_count, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&s_nanosec, &r_nanosec, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);

        test.test_count = r_test_count;
        test.nanosec = r_nanosec;

        int rank;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (rank == 0)
        {
            std::cout << std::left
                      << std::setw(22) << test.test_size << " | "
                      << std::setw(06) << test.test_count << " | "
                      << std::setw(22) << (double)test.nanosec / 1'000'000.0 / (double)test.test_count << " | "
                      << std::setw(22) << r_bw
                      << std::endl;
        }
    }

    class timer
    {
    public:
        timer()
        {
            resetElapsedNano();
        }

        uint64_t resetElapsedNano()
        {
            uint64_t out = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - m_Start).count();
            m_Start = std::chrono::high_resolution_clock::now();
            return out;
        }

    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> m_Start;
    };
} // namespace bw_examples