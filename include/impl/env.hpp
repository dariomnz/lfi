
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

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace LFI {
class env {
   public:
    template <typename T>
    void parse_env(const char *env, T &value) {
        char *endptr;
        int int_value;

        char *env_value = std::getenv(env);
        if ((env_value == NULL) || (std::strlen(env_value) == 0)) {
            return;
        }

        int_value = (int)std::strtol(env_value, &endptr, 10);
        if ((endptr == env_value) || (*endptr != '\0')) {
            std::cerr << "Warning: environmental variable '" << env << "' with value '" << env_value
                      << "' is not a number" << std::endl;
            return;
        }

        value = int_value;
    }

    env() {
        parse_env("LFI_FAULT_TOLERANCE", LFI_fault_tolerance);
        parse_env("LFI_FAULT_TOLERANCE_TIME", LFI_fault_tolerance_time);
        parse_env("LFI_PORT", LFI_port);
        parse_env("LFI_GROUP_PORT", LFI_group_port);
        parse_env("LFI_MS_WAIT_SLEEP", LFI_ms_wait_sleep);
        parse_env("LFI_USE_INJECT", LFI_use_inject);
        parse_env("LFI_ASYNC_CONNECTION", LFI_async_connection);
        parse_env("LFI_EFFICIENT_PROGRESS", LFI_efficient_progress);
        parse_env("LFI_LD_PRELOAD_THREADS", LFI_ld_preload_threads);
        parse_env("LFI_LD_PRELOAD_BUFFERED", LFI_ld_preload_buffered);
        parse_env("LFI_DEBUG_DUMP_INTERVAL", LFI_debug_dump_interval);
        parse_env("LFI_USE_SHM", LFI_use_shm);
        parse_env("LFI_DEBUG", LFI_debug);
    }
    // Delete copy constructor
    env(const env &) = delete;
    // Delete copy assignment operator
    env &operator=(const env &) = delete;
    // Delete move constructor
    env(env &&) = delete;
    // Delete move assignment operator
    env &operator=(env &&) = delete;
    bool LFI_fault_tolerance = true;
    int LFI_fault_tolerance_time = 5;
    int LFI_port = 56789;
    int LFI_group_port = 56790;
    int LFI_ms_wait_sleep = 10;
    bool LFI_use_inject = false;
    bool LFI_async_connection = true;
    bool LFI_efficient_progress = true;
    size_t LFI_ld_preload_threads = 1;
    size_t LFI_ld_preload_buffered = 64 * 1024;
    int LFI_debug_dump_interval = 0;
    bool LFI_use_shm = true;
    bool LFI_debug = true;

   public:
    static env &get_instance() {
        static env instance;
        return instance;
    }
};
}  // namespace LFI