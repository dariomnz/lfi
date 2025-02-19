
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
#include <thread>
#include <string>
#include <algorithm>

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
        std::atomic_uint64_t test_tag = 0;

        std::atomic_uint64_t size = 0;
        std::atomic_uint64_t nanosec = 0;
    };

    static std::vector<bw_test> &get_test_vector()
    {
        static std::vector<bw_test> tests(30);
        static bool initialized = false;
        if (!initialized)
        {
            initialized = true;

            for (size_t i = 0; i < tests.size(); i++)
            {
                tests[i].test_size = 1ull << i;
                tests[i].test_count = 1;
                tests[i].test_tag = 1000+i;
            }
        }
        return tests;
    }

    [[maybe_unused]] static std::vector<std::string> split(std::string s, std::string delimiter) {
        std::vector<std::string> tokens;
        size_t pos = 0;
        std::string token;
        while ((pos = s.find(delimiter)) != std::string::npos) {
            token = s.substr(0, pos);
            tokens.push_back(token);
            s.erase(0, pos + delimiter.length());
        }
        tokens.push_back(s);

        return tokens;
    }

    [[maybe_unused]] static void print_header()
    {
        int rank;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (rank == 0)
        {
            std::cout << std::left
                      << std::setw( 8) << "Msg size" << " | "
                      << std::setw( 5) << "Count" << " | "
                      << std::setw( 7) << "Size" << " | "
                      << std::setw( 8) << "Time (s)" << " | "
                      << std::setw(12) << "Latency (ms)" << " | "
                      << std::setw(16) << "Bandwidth (MB/s)" << " | "
                      << std::setw(16) << "Msg rate (msg/s)"
                      << std::endl;
        }
    }

    [[maybe_unused]] static std::string bytes_to_str(uint64_t bytes) 
    {
        const uint64_t TERABYTE = 1024ull * 1024 * 1024 * 1024;
        const uint64_t GIGABYTE = 1024ull * 1024 * 1024;
        const uint64_t MEGABYTE = 1024ull * 1024;
        const uint64_t KILOBYTE = 1024ull;

        double value = 0.0;
        std::string unit;

        if (bytes >= TERABYTE) {
            value = static_cast<double>(bytes) / TERABYTE;
            unit = "TB";
        } else if (bytes >= GIGABYTE) {
            value = static_cast<double>(bytes) / GIGABYTE;
            unit = "GB";
        } else if (bytes >= MEGABYTE) {
            value = static_cast<double>(bytes) / MEGABYTE;
            unit = "MB";
        } else if (bytes >= KILOBYTE) {
            value = static_cast<double>(bytes) / KILOBYTE;
            unit = "KB";
        } else {
            value = static_cast<double>(bytes);
            unit = "B";
        }

        std::ostringstream result;
        if (value == static_cast<int64_t>(value)) {
            result << static_cast<int64_t>(value) << unit;
        } else {
            result << std::fixed << std::setprecision(2) << value << unit;
        }
        return result.str();
    }

    [[maybe_unused]] static void print_test(bw_test &test)
    {
        uint64_t s_test_count = test.test_count;
        uint64_t s_size = test.size;

        uint64_t r_test_count = 0;
        uint64_t r_size = 0;

        // MPI_Reduce(&s_bw, &r_bw, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&s_test_count, &r_test_count, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&s_size, &r_size, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Barrier(MPI_COMM_WORLD);

        double sec =  ((double)test.nanosec / 1'000'000'000.0);
        double bw = ((double)r_size / (double)MB) / sec;
        test.test_count = r_test_count;

        int rank;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (rank == 0)
        {
            std::cout << std::left
                      << std::setw( 8) << bytes_to_str(test.test_size) << " | "
                      << std::setw( 5) << test.test_count << " | "
                      << std::setw( 7) << bytes_to_str(r_size) << " | "
                      << std::setw( 8) << std::fixed << std::setprecision(5) << sec << " | "
                      << std::setw(12) << std::fixed << std::setprecision(5) << (double)test.nanosec / 1'000'000.0 / (double)test.test_count << " | "
                      << std::setw(16) << bw << " | "
                      << std::setw(16) << std::fixed << std::setprecision(0) << (double)test.test_count / ((double)test.nanosec / 1'000'000'000.0)
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