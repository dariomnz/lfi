
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

#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <iomanip>
#include <chrono>

namespace LFI {
constexpr const char *file_name(const char *path) {
    const char *file = path;
    while (*path) {
        if (*path++ == '/') {
            file = path;
        }
    }
    return file;
}

static inline std::string getTime()
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm = *std::localtime(&now_c);

    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << local_tm.tm_hour << ":"
        << std::setw(2) << std::setfill('0') << local_tm.tm_min << ":"
        << std::setw(2) << std::setfill('0') << local_tm.tm_sec << ":"
        << std::setw(3) << std::setfill('0') << milliseconds.count();
    
    return oss.str();
}

class debug_lock {
   public:
    static std::mutex &get_lock() {
        static std::mutex mutex;
        return mutex;
    }
};

#define print_error(out_format)                                                                                    \
    {                                                                                                              \
        std::unique_lock internal_debug_lock(::LFI::debug_lock::get_lock());                                       \
        std::cerr << std::dec << ::LFI::getTime() << " [ERROR] [" << ::LFI::file_name(__FILE__) << ":" << __LINE__ << "] [" << __func__ \
                  << "] [" << std::this_thread::get_id() << "] " << out_format << " : " << std::strerror(errno)    \
                  << std::endl                                                                                     \
                  << std::flush;                                                                                   \
    }

#ifdef DEBUG
#define debug_error(out_format)                                                                                    \
    {                                                                                                              \
        std::unique_lock internal_debug_lock(::LFI::debug_lock::get_lock());                                       \
        std::cerr << std::dec << ::LFI::getTime() << " [ERROR] [" << ::LFI::file_name(__FILE__) << ":" << __LINE__ << "] [" << __func__ \
                  << "] [" << std::this_thread::get_id() << "] " << out_format << std::endl                        \
                  << std::flush;                                                                                   \
    }
#define debug_warning(out_format)                                                                                    \
    {                                                                                                                \
        std::unique_lock internal_debug_lock(::LFI::debug_lock::get_lock());                                         \
        std::cerr << std::dec << ::LFI::getTime() << " [WARNING] [" << ::LFI::file_name(__FILE__) << ":" << __LINE__ << "] [" << __func__ \
                  << "] [" << std::this_thread::get_id() << "] " << out_format << std::endl                          \
                  << std::flush;                                                                                     \
    }
#define debug_info(out_format)                                                                                    \
    {                                                                                                             \
        std::unique_lock internal_debug_lock(::LFI::debug_lock::get_lock());                                      \
        std::cerr << std::dec << ::LFI::getTime() << " [INFO] [" << ::LFI::file_name(__FILE__) << ":" << __LINE__ << "] [" << __func__ \
                  << "] [" << std::this_thread::get_id() << "] " << out_format << std::endl                       \
                  << std::flush;                                                                                  \
    }
#else
#define debug_error(out_format)
#define debug_warning(out_format)
#define debug_info(out_format)
#endif

#define print(out_format)                                                    \
    {                                                                        \
        std::unique_lock internal_debug_lock(::LFI::debug_lock::get_lock()); \
        std::cerr << std::dec << out_format << std::endl << std::flush;      \
    }

}  // namespace LFI