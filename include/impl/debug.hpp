
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

#include <linux/limits.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#include "env.hpp"

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

static inline std::string file_exe_name() {
    char buffer[PATH_MAX];
    ssize_t longitud = readlink("/proc/self/exe", buffer, PATH_MAX - 1);
    if (longitud != -1) {
        buffer[longitud] = '\0';
        return std::string(file_name(buffer));
    } else {
        return "";
    }
}

template <typename clock>
struct format_time {
    std::chrono::time_point<clock> m_point;
    format_time(std::chrono::time_point<clock> point) : m_point(point) {}
    friend std::ostream &operator<<(std::ostream &os, const format_time<clock> &ftime) {
        std::time_t now_c = clock::to_time_t(ftime.m_point);
        std::tm local_tm{};
        ::localtime_r(&now_c, &local_tm);

        auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(ftime.m_point.time_since_epoch()) % 1000;

        os << std::setw(2) << std::setfill('0') << local_tm.tm_hour << ":" << std::setw(2) << std::setfill('0')
           << local_tm.tm_min << ":" << std::setw(2) << std::setfill('0') << local_tm.tm_sec << ":" << std::setw(3)
           << std::setfill('0') << milliseconds.count();

        return os;
    }
};

static inline std::string getTime() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
    ::localtime_r(&now_c, &local_tm);

    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << local_tm.tm_hour << ":" << std::setw(2) << std::setfill('0')
        << local_tm.tm_min << ":" << std::setw(2) << std::setfill('0') << local_tm.tm_sec << ":" << std::setw(3)
        << std::setfill('0') << milliseconds.count();

    return oss.str();
}

static inline std::mutex &get_print_mutex() {
    static std::mutex print_mutex;
    return print_mutex;
}

#undef print_error
#define print_error(out_format)                                                                                \
    {                                                                                                          \
        std::ostringstream __out;                                                                              \
        __out << std::dec << ::LFI::getTime() << " [ERROR] [" << ::LFI::file_name(__FILE__) << ":" << __LINE__ \
              << "] [" << __func__ << "] [" << std::this_thread::get_id() << "] " << out_format << " : "       \
              << std::strerror(errno) << std::endl;                                                            \
        fprintf(stderr, "%s", __out.str().c_str());                                                            \
        fflush(stderr);                                                                                        \
    }

#ifdef DEBUG
#define debug_error(out_format)                                                                                \
    if (::LFI::env::get_instance().LFI_debug) {                                                                \
        std::unique_lock<std::mutex> lock(::LFI::get_print_mutex());                                           \
        std::ostringstream __out;                                                                              \
        __out << std::dec << ::LFI::getTime() << " [ERROR] [" << ::LFI::file_name(__FILE__) << ":" << __LINE__ \
              << "] [" << __func__ << "] [" << std::this_thread::get_id() << "] " << out_format << std::endl;  \
        fprintf(stderr, "%s", __out.str().c_str());                                                            \
        fflush(stderr);                                                                                        \
    }
#define debug_warning(out_format)                                                                                \
    if (::LFI::env::get_instance().LFI_debug) {                                                                  \
        std::unique_lock<std::mutex> lock(::LFI::get_print_mutex());                                             \
        std::ostringstream __out;                                                                                \
        __out << std::dec << ::LFI::getTime() << " [WARNING] [" << ::LFI::file_name(__FILE__) << ":" << __LINE__ \
              << "] [" << __func__ << "] [" << std::this_thread::get_id() << "] " << out_format << std::endl;    \
        fprintf(stderr, "%s", __out.str().c_str());                                                              \
        fflush(stderr);                                                                                          \
    }
#define debug_info(out_format)                                                                                         \
    if (::LFI::env::get_instance().LFI_debug) {                                                                        \
        std::unique_lock<std::mutex> lock(::LFI::get_print_mutex());                                                   \
        std::ostringstream __out;                                                                                      \
        __out << std::dec << ::LFI::getTime() << " [INFO] [" << ::LFI::file_name(__FILE__) << ":" << __LINE__ << "] [" \
              << __func__ << "] [" << std::this_thread::get_id() << "] " << out_format << std::endl;                   \
        fprintf(stderr, "%s", __out.str().c_str());                                                                    \
        fflush(stderr);                                                                                                \
    }
#else
#define debug_error(out_format)
#define debug_warning(out_format)
#define debug_info(out_format)
#endif

#undef print
#define print(out_format)                                            \
    {                                                                \
        std::unique_lock<std::mutex> lock(::LFI::get_print_mutex()); \
        std::ostringstream __out;                                    \
        __out << std::dec << out_format << std::endl;                \
        fprintf(stdout, "%s", __out.str().c_str());                  \
        fflush(stdout);                                              \
    }

class DeferAction {
   public:
    DeferAction(std::function<void()> action) : m_action(action) {}
    ~DeferAction() {
        if (m_action) {
            m_action();
        }
    }

   private:
    std::function<void()> m_action;
};

#define ____defer(action, line) DeferAction defer_object_##line(action)
#define __defer(action, line)   ____defer(action, line)
#define defer(action)           __defer(action, __LINE__)

}  // namespace LFI