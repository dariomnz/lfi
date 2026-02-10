
/*
 *  Copyright 2024-2025 Dario Muñoz Muñoz, Felix Garcia Carballeira, Diego Camarmas Alonso, Alejandro Calderon Mateos
 *
 *  This file is part of LFI.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace LFI {

class LFI;
struct lfi_endpoint;
struct lfi_comm;
struct lfi_request;

class lfi_ft_manager {
   public:
    explicit lfi_ft_manager(LFI& lfi);
    ~lfi_ft_manager();

    // Prevent copying
    lfi_ft_manager(const lfi_ft_manager&) = delete;
    lfi_ft_manager& operator=(const lfi_ft_manager&) = delete;

    void start();
    void stop();

    // Called when a request starts to register it for monitoring
    void register_request(lfi_request* req, uint32_t tag, lfi_comm* comm);

    // Called when a request finishes to update last_request_time and clean up ft_requests
    void on_request_complete(lfi_request* req, int& err);

    // Core monitoring loop executed by the FT thread or progress
    void one_loop(lfi_endpoint& ep);

    void setup_ping_pongs();

    int cancel_comm(lfi_comm& comm);

    bool is_running() const { return m_running.load(); }
    void notify_one() { m_cv.notify_one(); }

    // Wait for the CV (used by the background thread)
    void wait_for(std::unique_lock<std::mutex>& lock, std::chrono::milliseconds ms) { m_cv.wait_for(lock, ms); }

    std::mutex& get_mutex() { return m_mutex; }

   private:
    void thread_loop();
    void process_comm(lfi_comm* comm, int32_t ft_ms, std::vector<uint32_t>& canceled_comms);
    void handle_any_comm_reports(lfi_endpoint& ep, std::vector<uint32_t>& canceled_comms);

    LFI& m_lfi;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running{false};
    std::vector<lfi_request*> m_requests_to_cancel;
    std::vector<uint32_t> m_canceled_coms;
};

}  // namespace LFI
