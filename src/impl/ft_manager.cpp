
#include "impl/ft_manager.hpp"

#include <mutex>
#include <stdexcept>
#include <string>

#include "helpers.hpp"
#include "impl/debug.hpp"
#include "impl/env.hpp"
#include "impl/lfi.hpp"
#include "impl/profiler.hpp"
#include "lfi.h"
#include "lfi_error.h"

namespace LFI {

lfi_ft_manager::lfi_ft_manager(LFI &lfi) : m_lfi(lfi) {}

lfi_ft_manager::~lfi_ft_manager() { stop(); }

void lfi_ft_manager::start() {
    LFI_PROFILE_FUNCTION();
    if (!env::get_instance().LFI_fault_tolerance) return;

    debug_info("[LFI][FT] Manager Start");
    {
        std::unique_lock lock(m_mutex);
        if (m_running) return;
        m_running = true;
    }
    m_thread = std::thread(&lfi_ft_manager::thread_loop, this);
    setup_ping_pongs();
    debug_info("[LFI][FT] Manager End Start");
}

void lfi_ft_manager::stop() {
    LFI_PROFILE_FUNCTION();
    if (!env::get_instance().LFI_fault_tolerance) return;

    debug_info("[LFI][FT] Manager Stop");
    {
        std::unique_lock lock(m_mutex);
        if (!m_running) return;
        m_running = false;
        m_cv.notify_one();
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    debug_info("[LFI][FT] Manager End Stop");
}

void lfi_ft_manager::thread_loop() {
    LFI_PROFILE_FUNCTION();
    debug_info("[LFI][FT] Thread Loop Start");

    auto last_debug_dump = std::chrono::high_resolution_clock::now();

    std::unique_lock lock(m_mutex);
    while (m_running) {
        if (env::get_instance().LFI_debug_dump_interval > 0) {
            auto now = std::chrono::high_resolution_clock::now();
            auto ellapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_debug_dump).count();
            if (ellapsed > (env::get_instance().LFI_debug_dump_interval * 1000)) {
                last_debug_dump = now;
                m_lfi.dump_stats();
            }
        }

        int progresed_shm = 0;
        int progresed_peer = 0;
        if (env::get_instance().LFI_use_shm) {
            {
                ProgressGuard shm_progress(m_lfi.shm_ep);
                if (shm_progress.is_leader()) {
                    progresed_shm = m_lfi.shm_ep.progress(true);
                }
            }
            one_loop(m_lfi.shm_ep);
        }
        {
            ProgressGuard peer_progress(m_lfi.peer_ep);
            if (peer_progress.is_leader()) {
                progresed_peer = m_lfi.peer_ep.progress(true);
            }
        }
        one_loop(m_lfi.peer_ep);

        if (progresed_shm > 0 || progresed_peer > 0) continue;

        m_cv.wait_for(lock, std::chrono::milliseconds(1));
    }
    debug_info("[LFI][FT] Thread Loop End");
}

static void pong_callback([[maybe_unused]] int error, void *context) {
    lfi_request *req = static_cast<lfi_request *>(context);
    delete req;
}

static void ping_callback([[maybe_unused]] int error, void *context) {
    static int dummy = 0;
    LFI &lfi = LFI::get_instance();
    lfi_request *req = static_cast<lfi_request *>(context);
    if (error == LFI_SUCCESS) {
        debug_info("[LFI] send pong to " << req->source);
        auto [lock, comm] = lfi.get_comm_and_mutex(req->source);
        if (!comm) {
            print("Error get_comm of " << req->source);
            return;
        }
        auto fi_pong = new lfi_request(comm->m_endpoint, comm->rank_peer);
        fi_pong->callback = pong_callback;
        fi_pong->callback_ctx = fi_pong;
        int ret = lfi.async_send(&dummy, 0, LFI_TAG_FT_PONG, *fi_pong, true);
        if (ret < 0) {
            print("Error in async_send " << ret << " " << lfi_strerror(ret));
            return;
        }
    }

    // Repost recv PING
    int ret = lfi.async_recv(&dummy, 0, LFI_TAG_FT_PING, *req, true);
    if (ret < 0) {
        print("Error in async_recv " << ret << " " << lfi_strerror(ret));
        return;
    }
}

void lfi_ft_manager::setup_ping_pongs() {
    LFI_PROFILE_FUNCTION();
    static int dummy = 0;

    auto create_ping = [this](auto any_comm) {
        auto [lock, comm] = m_lfi.get_comm_and_mutex(any_comm);
        if (!comm) {
            print("Error get_comm ANY_COMM_SHM " << any_comm);
            return -1;
        }
        auto &ft_ping = comm->m_endpoint.ft_ping_pongs.emplace_back(
            std::make_unique<lfi_request>(comm->m_endpoint, comm->rank_peer));
        ft_ping->callback = ping_callback;
        ft_ping->callback_ctx = ft_ping.get();
        int ret = m_lfi.async_recv(&dummy, 0, LFI_TAG_FT_PING, *ft_ping, true);
        if (ret < 0) {
            print("Error in async_recv");
            return ret;
        }
        return 0;
    };

    const size_t ping_pong_count = 8;
    m_lfi.shm_ep.ft_ping_pongs.reserve(ping_pong_count);
    m_lfi.peer_ep.ft_ping_pongs.reserve(ping_pong_count);
    for (size_t i = 0; i < ping_pong_count; i++) {
        if (create_ping(LFI_ANY_COMM_SHM) < 0) {
            print("Error create_ping LFI_ANY_COMM_SHM");
        }
        if (create_ping(LFI_ANY_COMM_PEER) < 0) {
            print("Error create_ping LFI_ANY_COMM_PEER");
        }
    }
}

void lfi_ft_manager::register_request(lfi_request *req, uint32_t tag, lfi_comm *comm) {
    if (!env::get_instance().LFI_fault_tolerance) return;

    if (comm->rank_peer == ANY_COMM_SHM || comm->rank_peer == ANY_COMM_PEER) {
        if (tag != LFI_TAG_FT_PING && tag != LFI_TAG_FT_PONG) {
            std::unique_lock lock(comm->m_endpoint.ft_mutex);
            debug_info("Save request in any_comm_requests " << req);
            comm->m_endpoint.ft_any_comm_requests.emplace(req);
        }
    } else {
        std::scoped_lock lock(comm->m_endpoint.ft_mutex, comm->ft_mutex);
        if (comm->ft_requests.size() == 0) {
            comm->m_endpoint.ft_comms.emplace(comm);
        }
    }

    {
        std::unique_lock ft_lock(comm->ft_mutex);
        comm->ft_requests.emplace(req);
        debug_info("[LFI] emplace request " << std::hex << req << std::dec << " in comm " << comm->rank_peer
                                            << " ft_requests size " << comm->ft_requests.size());
        debug_info(*req);
    }
}

void lfi_ft_manager::on_request_complete(lfi_request *req, int &err) {
    auto [lock, comm] = m_lfi.get_comm_and_mutex(req->m_comm_id);
    if (comm) {
        std::unique_lock ft_lock(comm->ft_mutex);
        comm->ft_requests.erase(req);
        {
            std::unique_lock lock_ep(req->m_endpoint.ft_mutex);
            if (req->m_comm_id != ANY_COMM_SHM && req->m_comm_id != ANY_COMM_PEER) {
                if (comm->ft_requests.size() == 0) {
                    req->m_endpoint.ft_comms.erase(comm);
                }
            } else {
                req->m_endpoint.ft_any_comm_requests.erase(req);
            }
        }
    }
    if (err == LFI_SUCCESS) {
        auto comm_ptr = m_lfi.get_comm_internal(lock, req->source);
        if (comm_ptr) {
            std::unique_lock ft_lock(comm_ptr->ft_mutex);
            comm_ptr->last_request_time = lfi_comm::clock::now();
        }
    }
}

void lfi_ft_manager::process_comm(lfi_comm *comm, int32_t ft_ms, std::vector<uint32_t> &canceled_coms) {
    static int dummy = 0;
    auto now = std::chrono::high_resolution_clock::now();
    if (comm->ft_current_status == lfi_comm::ft_status::IDLE) {
        decltype(comm->last_request_time) last_request_time;
        {
            std::unique_lock ft_lock(comm->ft_mutex);
            last_request_time = comm->last_request_time;
        }
        int32_t elapsed_ms_req = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_request_time).count();
        if (elapsed_ms_req > ft_ms) {
            // Initiate Pinging
            if (!comm->ft_ping) {
                comm->ft_ping = std::make_unique<lfi_request>(comm->m_endpoint, comm->rank_peer);
            } else {
                comm->ft_ping->reset();
            }
            if (!comm->ft_pong) {
                comm->ft_pong = std::make_unique<lfi_request>(comm->m_endpoint, comm->rank_peer);
            } else {
                comm->ft_pong->reset();
            }

            // Post recv PONG first
            auto ret_pong = m_lfi.async_recv(&dummy, 0, LFI_TAG_FT_PONG, *comm->ft_pong, true);
            if (ret_pong < 0) {
                canceled_coms.emplace_back(comm->rank_peer);
                comm->ft_current_status = lfi_comm::ft_status::ERROR;
                return;
            }

            // Send PING
            auto ret_ping = m_lfi.async_send(&dummy, 0, LFI_TAG_FT_PING, *comm->ft_ping, true);
            if (ret_ping < 0) {
                canceled_coms.emplace_back(comm->rank_peer);
                comm->ft_current_status = lfi_comm::ft_status::ERROR;
                return;
            }

            comm->ft_current_status = lfi_comm::ft_status::PINGING;
            comm->ft_ping_pong_time_point = std::chrono::high_resolution_clock::now();
        }
    } else if (comm->ft_current_status == lfi_comm::ft_status::PINGING) {
        std::scoped_lock ping_pong_lock(comm->ft_ping->mutex, comm->ft_pong->mutex);
        if (comm->ft_ping->is_completed() && comm->ft_pong->is_completed()) {
            comm->ft_current_status = lfi_comm::ft_status::IDLE;
        } else {
            int32_t elapsed_ms_pp =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - comm->ft_ping_pong_time_point).count();
            if (elapsed_ms_pp > ft_ms) {
                canceled_coms.emplace_back(comm->rank_peer);
                comm->ft_current_status = lfi_comm::ft_status::ERROR;
            }
        }
    }
}

void lfi_ft_manager::handle_any_comm_reports(lfi_endpoint &lfi_ep, std::vector<uint32_t> &canceled_coms) {
    {
        std::scoped_lock lock(lfi_ep.ft_mutex);
        if (lfi_ep.ft_any_comm_requests.empty() && lfi_ep.ft_pending_failed_comms.empty() && canceled_coms.empty())
            return;
    }

    m_requests_to_cancel.reserve(10);

    {
        std::unique_lock ft_ep_lock(lfi_ep.ft_mutex);

        auto report_to_any = [&](auto &error_sources, bool consume) {
            auto any_req_it = lfi_ep.ft_any_comm_requests.begin();
            auto error_it = error_sources.begin();

            while (any_req_it != lfi_ep.ft_any_comm_requests.end() && error_it != error_sources.end()) {
                lfi_request *any_req = *any_req_it;
                uint32_t failed_comm = *error_it;

                {
                    std::unique_lock req_lock(any_req->mutex);
                    any_req->source = failed_comm;
                    any_req->error = -LFI_BROKEN_COMM;
                }
                m_requests_to_cancel.push_back(any_req);

                any_req_it = lfi_ep.ft_any_comm_requests.erase(any_req_it);
                if (consume) {
                    error_it = error_sources.erase(error_it);
                } else {
                    ++error_it;
                }
            }
            return error_it;
        };

        report_to_any(lfi_ep.ft_pending_failed_comms, true);

        if (!canceled_coms.empty()) {
            auto next_it = report_to_any(canceled_coms, false);
            while (next_it != canceled_coms.end()) {
                lfi_ep.ft_pending_failed_comms.emplace(*next_it);
                ++next_it;
            }
        }
    }

    for (auto req : m_requests_to_cancel) {
        req->cancel();
    }
    m_requests_to_cancel.clear();
}

void lfi_ft_manager::one_loop(lfi_endpoint &lfi_ep) {
    LFI_PROFILE_FUNCTION();

    m_canceled_coms.reserve(10);
    int32_t ft_ms = std::max(1000, env::get_instance().LFI_fault_tolerance_time * 1000);

    {
        std::shared_lock comm_lock(m_lfi.m_comms_mutex);
        std::unique_lock ft_ep_lock(lfi_ep.ft_mutex);

        if (lfi_ep.ft_any_comm_requests.size() > 0) {
            ft_ep_lock.unlock();
            for (auto &&[comm_id, comm] : m_lfi.m_comms) {
                if (comm && comm->m_endpoint == lfi_ep && comm->rank_peer != ANY_COMM_SHM &&
                    comm->rank_peer != ANY_COMM_PEER) {
                    process_comm(comm.get(), ft_ms, m_canceled_coms);
                }
            }
        } else {
            std::unordered_set<lfi_comm *> temp_ft_comms(lfi_ep.ft_comms);
            ft_ep_lock.unlock();
            for (auto &&comm : temp_ft_comms) {
                process_comm(comm, ft_ms, m_canceled_coms);
            }
        }

        for (auto &&comm_id : m_canceled_coms) {
            auto comm_ptr = m_lfi.get_comm_internal(comm_lock, comm_id);
            if (comm_ptr) cancel_comm(*comm_ptr);
        }
    }

    handle_any_comm_reports(lfi_ep, m_canceled_coms);
    m_canceled_coms.clear();
}

int lfi_ft_manager::cancel_comm(lfi_comm &comm) {
    LFI_PROFILE_FUNCTION();
    std::unique_lock lock(comm.ft_mutex);
    comm.is_canceled = true;
    debug_info("[LFI] Canceling comm with rank " << comm.rank_peer);
    std::unordered_set<lfi_request *> temp_requests(comm.ft_requests);
    for (auto &request : temp_requests) {
        if (request == nullptr) continue;
        request->cancel();
    }
    {
        std::unique_lock ft_lock(comm.m_endpoint.ft_mutex);
        comm.m_endpoint.ft_comms.erase(&comm);
    }
    comm.ft_requests.clear();
    return 0;
}

}  // namespace LFI
