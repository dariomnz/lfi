
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

namespace LFI
{
    class env
    {
    public:
        env()
        {
            // LFI_FAULT_TOLERANCE
            char *env_lfi_fault_tolerance = std::getenv("LFI_FAULT_TOLERANCE");
            if ((env_lfi_fault_tolerance != NULL) && (std::strlen(env_lfi_fault_tolerance) > 0))
            {
                LFI_fault_tolerance = (atoi(env_lfi_fault_tolerance) != 0);
            }
            // LFI_FAULT_TOLERANCE_TIME
            char *env_lfi_fault_tolerance_time = std::getenv("LFI_FAULT_TOLERANCE_TIME");
            if ((env_lfi_fault_tolerance_time != NULL) && (std::strlen(env_lfi_fault_tolerance_time) > 0))
            {
                LFI_fault_tolerance_time = atoi(env_lfi_fault_tolerance_time);
            }
            // LFI_PORT
            char *env_lfi_port = std::getenv("LFI_PORT");
            if ((env_lfi_port != NULL) && (std::strlen(env_lfi_port) > 0))
            {
                LFI_port = atoi(env_lfi_port);
            }
            // LFI_MS_WAIT_SLEEP
            char *env_lfi_ms_wait_sleep = std::getenv("LFI_MS_WAIT_SLEEP");
            if ((env_lfi_ms_wait_sleep != NULL) && (std::strlen(env_lfi_ms_wait_sleep) > 0))
            {
                LFI_ms_wait_sleep = atoi(env_lfi_ms_wait_sleep);
            }
            // LFI_USE_INJECT
            char *env_lfi_use_inject = std::getenv("LFI_USE_INJECT");
            if ((env_lfi_use_inject != NULL) && (std::strlen(env_lfi_use_inject) > 0))
            {
                LFI_use_inject = (atoi(env_lfi_use_inject) != 0);
            }
            // LFI_ASYNC_CONNECTION
            char *env_lfi_async_connection = std::getenv("LFI_ASYNC_CONNECTION");
            if ((env_lfi_async_connection != NULL) && (std::strlen(env_lfi_async_connection) > 0))
            {
                LFI_async_connection = (atoi(env_lfi_async_connection) != 0);
            }
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
        int LFI_ms_wait_sleep = 10;
        bool LFI_use_inject = false;
        bool LFI_async_connection = true;

    public:
        static env &get_instance()
        {
            static env instance;
            return instance;
        }
    };
} // namespace LFI