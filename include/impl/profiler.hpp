
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

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "debug.hpp"

namespace LFI {

struct profiler_data {
    pid_t m_pid;
    std::thread::id m_tid;
    std::variant<const char*, std::string> m_name;
    uint32_t m_start;
    uint32_t m_duration;

    profiler_data(pid_t pid, std::thread::id tid, std::variant<const char*, std::string> name, uint32_t start,
                  uint32_t duration)
        : m_pid(pid), m_tid(tid), m_name(name), m_start(start), m_duration(duration) {}

    void dump_data(std::ostream& json, bool first) const {
        if (!first) {
            json << ",";
        }
        json << "{";
        json << "\"cat\":\"function\",";
        json << "\"dur\":" << m_duration << ',';
        json << "\"tdur\":" << m_duration << ',';
        if (std::holds_alternative<const char*>(m_name)) {
            json << "\"name\":\"" << std::get<const char*>(m_name) << "\",";
        } else {
            json << "\"name\":\"" << std::get<std::string>(m_name) << "\",";
        }
        json << "\"ph\":\"X\",";
        json << "\"pid\":\"" << m_pid << "\",";
        json << "\"tid\":" << m_tid << ",";
        json << "\"ts\":" << m_start << ",";
        json << "\"tts\":" << m_start;
        json << "}\n";
    }
};

class profiler {
   public:
    profiler(const profiler&) = delete;
    profiler(profiler&&) = delete;

    profiler() {
        std::string file_name = file_exe_name();
        file_name += ".profile.";
        file_name += std::to_string(getpid());
        file_name += ".json";
        begin_session(file_name);
    }

    ~profiler() { end_session(); }

    void begin_session(const std::string& name) {
        std::unique_lock lock(m_mutex);
        m_buffer = std::vector<profiler_data>();
        m_buffer.reserve(m_buffer_cap);
        m_current_session_file = name;

        std::unique_lock write_lock(m_write_mutex);
        std::ofstream output_file(m_current_session_file, std::ios::out);

        if (output_file.is_open()) {
            output_file << get_header();
            output_file.close();
        } else {
            std::cerr << "ERROR: Could not open file " << m_current_session_file << " for writing.\n";
        }
    }

    void end_session() {
        std::unique_lock lock(m_mutex);
        if (m_buffer.size() > 0) {
            save_data(std::move(m_buffer));
        }
        for (auto& fut : m_fut_save_data) {
            if (fut.valid()) {
                fut.get();
            }
        }
        std::unique_lock write_lock(m_write_mutex);
        std::ofstream output_file(m_current_session_file, std::ios::out | std::ios::app);

        if (output_file.is_open()) {
            output_file << get_footer();
            output_file.close();
        } else {
            std::cerr << "ERROR: Could not open file " << m_current_session_file << " for writing.\n";
        }
    }

    void write_profile(std::variant<const char*, std::string> name, uint32_t start, uint32_t duration) {
        std::unique_lock lock(m_mutex);
        if (!m_current_session_file.empty()) {
            m_buffer.emplace_back(getpid(), std::this_thread::get_id(), name, start, duration);

            if (m_buffer.size() >= m_buffer_cap) {
                save_data(std::move(m_buffer));
                m_buffer = std::vector<profiler_data>();
                m_buffer.reserve(m_buffer_cap);
            }
        }
    }

    static std::string get_header() { return "{\"otherData\": {},\"traceEvents\":[\n"; }
    static std::string get_footer() { return "]}"; }

    static profiler& get_instance() {
        static profiler instance;
        return instance;
    }

   private:
    void save_data(const std::vector<profiler_data>&& message) {
        m_fut_save_data.erase(
            std::remove_if(m_fut_save_data.begin(), m_fut_save_data.end(),
                           [](auto& fut) {
                               if (fut.valid() && fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                                   fut.get();
                                   return true;
                               }
                               return false;
                           }),
            m_fut_save_data.end());

        m_fut_save_data.emplace_back(std::async(
            std::launch::async, [msg = std::move(message), current_session_file = m_current_session_file, this]() {
                std::stringstream ss;
                for (auto& data : msg) {
                    data.dump_data(ss, m_first_dump.exchange(false));
                }
                std::string str = ss.str();
                std::unique_lock write_lock(m_write_mutex);
                std::ofstream output_file(current_session_file, std::ios::out | std::ios::app);

                if (output_file.is_open()) {
                    output_file << str;
                    output_file.close();
                    return 0;
                } else {
                    std::cerr << "ERROR: Could not open file " << current_session_file << " for writing.\n";
                    return -1;
                }
                return 0;
            }));
    }

   private:
    std::mutex m_mutex;
    std::mutex m_write_mutex;
    std::string m_current_session_file = "";
    std::vector<std::future<int>> m_fut_save_data;
    constexpr static uint64_t m_buffer_cap = 1024;
    std::vector<profiler_data> m_buffer;
    std::atomic_bool m_first_dump = true;
};

class profiler_timer {
   public:
    profiler_timer(const char* name) : m_name(name) { m_start_timepoint = std::chrono::high_resolution_clock::now(); }

    template <typename... Args>
    profiler_timer(Args... args) {
        m_start_timepoint = std::chrono::high_resolution_clock::now();
        m_name_str = concatenate(args...);
    }

    template <typename T, typename... Rest>
    std::string concatenate(const T& value, const Rest&... rest) {
        std::stringstream ss;
        ss << value;
        std::string result = ss.str();
        if constexpr (sizeof...(rest) > 0) {
            result += ", " + concatenate(rest...);
        }
        return result;
    }

    ~profiler_timer() {
        if (!m_stopped) Stop();
    }

    void Stop() {
        auto start =
            std::chrono::duration_cast<std::chrono::microseconds>(m_start_timepoint.time_since_epoch()).count();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::high_resolution_clock::now() - m_start_timepoint)
                                .count();
        if (m_name == nullptr) {
            profiler::get_instance().write_profile(m_name_str, start, elapsed_time);
        } else {
            profiler::get_instance().write_profile(m_name, start, elapsed_time);
        }
        m_stopped = true;
    }

   private:
    const char* m_name = nullptr;
    std::string m_name_str;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start_timepoint;
    bool m_stopped = false;
};
}  // namespace LFI

#if LFI_PROFILE
#define __LFI_PROFILE_SCOPE_LINE2(name, line)     ::LFI::profiler_timer timer##line(name)
#define __LFI_PROFILE_SCOPE_LINE(name, line)      __LFI_PROFILE_SCOPE_LINE2(name, line)
#define LFI_PROFILE_SCOPE(name)                   __LFI_PROFILE_SCOPE_LINE(name, __LINE__)
#define LFI_PROFILE_FUNCTION()                    LFI_PROFILE_SCOPE(__func__)

#define __LFI_PROFILE_SCOPE_ARGS_LINE2(line, ...) ::LFI::profiler_timer timer##line(__VA_ARGS__)
#define __LFI_PROFILE_SCOPE_ARGS_LINE(line, ...)  __LFI_PROFILE_SCOPE_ARGS_LINE2(line, __VA_ARGS__)
#define LFI_PROFILE_SCOPE_ARGS(name, ...)         __LFI_PROFILE_SCOPE_ARGS_LINE(__LINE__, name, __VA_ARGS__)
#define LFI_PROFILE_FUNCTION_ARGS(...)            LFI_PROFILE_SCOPE_ARGS(__func__, __VA_ARGS__)
#else
#define LFI_PROFILE_SCOPE(name)
#define LFI_PROFILE_FUNCTION()

#define LFI_PROFILE_SCOPE_ARGS(name, ...)
#define LFI_PROFILE_FUNCTION_ARGS(...)
#endif