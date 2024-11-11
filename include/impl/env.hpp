
/*
 *  Copyright 2020-2024 Felix Garcia Carballeira, Diego Camarmas Alonso, Alejandro Calderon Mateos, Dario Muñoz Muñoz
 *
 *  This file is part of Expand.
 *
 *  Expand is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Expand is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Expand.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <cstdlib>
#include <cstring>

namespace LFI {
    class env
    {
    public:
        env(){
            // LFI_THREADS
            char *env_fabric_threads = std::getenv("LFI_THREADS");
            if ((env_fabric_threads != NULL) && (std::strlen(env_fabric_threads) > 0)){
                LFI_threads=atoi(env_fabric_threads);
            }
        }
        // Delete copy constructor
        env(const env&) = delete;
        // Delete copy assignment operator
        env& operator=(const env&) = delete;
        // Delete move constructor
        env(env&&) = delete;
        // Delete move assignment operator
        env& operator=(env&&) = delete;
        int LFI_threads = 10;
    public:
        static env& get_instance()
        {
            static env instance;
            return instance;
        }
    };
} // namespace LFI