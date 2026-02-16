
/*
 *  Copyright 2024-2025 Dario Muñoz Muñoz, Felix Garcia Carballeira, Diego Camarmas Alonso, Alejandro Calderon Mateos
 *
 *  This file is part of LFI.
 */

#pragma once

#include <cstdint>
#include <mutex>
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

    // Prevent copy and move
    lfi_ft_manager(const lfi_ft_manager&) = delete;
    lfi_ft_manager& operator=(const lfi_ft_manager&) = delete;
    lfi_ft_manager(lfi_ft_manager&&) = delete;
    lfi_ft_manager& operator=(lfi_ft_manager&&) = delete;

    void start();
    void stop();

    // Called when a request starts to register it for monitoring
    void register_request(lfi_request* req, lfi_comm* comm);

    // Called when a request finishes to update last_request_time and clean up ft_requests
    void on_request_complete(lfi_request* req, int& err);

    // Core monitoring loop executed by the FT thread or progress
    void one_loop(lfi_endpoint& ep);

    void setup_heartbeat();

    int cancel_comm(lfi_comm& comm);

    static constexpr uint64_t HEARBEAT_CODE = 0xBAADC0DEDEAD11FE;
    uint64_t m_local_heartbeat = HEARBEAT_CODE;
    int m_heartbeat_key = -1;

   private:
    void thread_loop();
    void process_comm(lfi_comm* comm, int32_t ft_ms, std::vector<uint32_t>& canceled_comms,
                      std::chrono::time_point<std::chrono::high_resolution_clock> now);
    void handle_any_comm_reports(lfi_endpoint& ep, std::vector<uint32_t>& canceled_comms);

    LFI& m_lfi;
    std::mutex m_mutex;
    bool m_initialized = false;
    std::vector<lfi_request*> m_requests_to_cancel;
    std::vector<uint32_t> m_canceled_coms;
};

}  // namespace LFI
