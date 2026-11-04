/*
 * Benchmark for lfi_mr_reg performance
 * Measures operations per second and average latency.
 */

#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <numeric>

#include "lfi.h"
#include "lfi_async.h"

// Benchmark Configuration
#define ITERATIONS 10000
#define BUFFER_SIZE 4 * 1024 * 1024  // 4 MB

int main() {
    // Prepare data buffer
    std::vector<uint8_t> rma_data(BUFFER_SIZE, 0);
    std::vector<lfi_mr_key> keys(ITERATIONS);
    
    std::cout << "=========================================" << std::endl;
    std::cout << "   LFI Memory Registration Benchmark     " << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << "Buffer Size:  " << (BUFFER_SIZE / 1024) << " KB" << std::endl;
    std::cout << "Iterations:   " << ITERATIONS << std::endl;
    std::cout << "Running... " << std::endl;

    // Use high_resolution_clock for microsecond precision
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        keys[i] = lfi_mr_reg(rma_data.data(), rma_data.size());

        if (keys[i].shm_key < 0 || keys[i].peer_key < 0) {
            std::cerr << "Error: lfi_mr_reg failed at iteration " << i << std::endl;
            return EXIT_FAILURE;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();

    // Calculate metrics
    std::chrono::duration<double> total_duration = end_time - start_time;
    double total_seconds = total_duration.count();
    double ops_per_sec = ITERATIONS / total_seconds;
    double avg_latency_us = (total_seconds * 1000.0) / ITERATIONS;

    // Use high_resolution_clock for microsecond precision
    auto start_time_unreg = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        auto ret = lfi_mr_unreg(keys[i]);

        if (ret < 0) {
            std::cerr << "Error: lfi_mr_unreg failed at iteration " << i << std::endl;
            return EXIT_FAILURE;
        }
    }

    auto end_time_unreg = std::chrono::high_resolution_clock::now();

    // Calculate metrics
    std::chrono::duration<double> total_duration_unreg = end_time_unreg - start_time_unreg;
    double total_seconds_unreg = total_duration_unreg.count();
    double ops_per_sec_unreg = ITERATIONS / total_seconds_unreg;
    double avg_latency_us_unreg = (total_seconds_unreg * 1000.0) / ITERATIONS;

    // Display Results
    std::cout << "--------------------reg---------------------" << std::endl;
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Total Execution Time: " << total_seconds << " s" << std::endl;
    std::cout << "Average Latency:      " << avg_latency_us << " ms/op" << std::endl;
    std::cout << "Throughput:           " << ops_per_sec << " registers/sec" << std::endl;
    std::cout << "-------------------unreg--------------------" << std::endl;
    std::cout << "Total Execution Time: " << total_seconds_unreg << " s" << std::endl;
    std::cout << "Average Latency:      " << avg_latency_us_unreg << " ms/op" << std::endl;
    std::cout << "Throughput:           " << ops_per_sec_unreg << " registers/sec" << std::endl;
    std::cout << "============================================" << std::endl;
}