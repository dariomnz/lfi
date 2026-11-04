
#include "impl/ft_manager.hpp"

#include <algorithm>
#include <optional>

#include "impl/debug.hpp"
#include "impl/env.hpp"
#include "impl/lfi.hpp"
#include "impl/profiler.hpp"
#include "lfi_error.h"

namespace LFI {

uint64_t lfi_ft_manager::m_local_heartbeat = HEARBEAT_CODE;

lfi_ft_manager::lfi_ft_manager(LFI &lfi) : m_lfi(lfi) {}

lfi_ft_manager::~lfi_ft_manager() { stop(); }

void lfi_ft_manager::start() {
    LFI_PROFILE_FUNCTION();
    if (!env::get_instance().LFI_fault_tolerance) return;

    debug_info("[LFI][FT] Manager Start");
    {
        std::unique_lock lock(m_mutex);
        if (m_initialized) return;
        m_initialized = true;
    }
    setup_heartbeat();
    debug_info("[LFI][FT] Manager End Start");
}

void lfi_ft_manager::stop() {
    LFI_PROFILE_FUNCTION();
    if (!env::get_instance().LFI_fault_tolerance) return;

    debug_info("[LFI][FT] Manager Stop");
    {
        std::unique_lock lock(m_mutex);
        if (!m_initialized) return;
        m_initialized = false;
    }
    if (m_heartbeat_key.shm_key >= 0) {
        m_lfi.mr_unreg(m_heartbeat_key);
        m_heartbeat_key = {-1, -1};
    }
    debug_info("[LFI][FT] Manager End Stop");
}

void lfi_ft_manager::setup_heartbeat() {
    LFI_PROFILE_FUNCTION();
    if (m_heartbeat_key.shm_key >= 0) return;
    m_heartbeat_key = m_lfi.mr_reg(&m_local_heartbeat, sizeof(m_local_heartbeat));
    if (m_heartbeat_key.shm_key < 0) {
        print_error("Error registering local heartbeat MR");
    }
}

void lfi_ft_manager::register_request(lfi_request *req, lfi_comm *comm) {
    if (!env::get_instance().LFI_fault_tolerance) return;

    // std::unique_lock ft_lock(comm->ft_mutex);
    if (comm->rank_peer == ANY_COMM_SHM || comm->rank_peer == ANY_COMM_PEER) {
        debug_info("Save request in any_comm_requests " << req);
        std::unique_lock end_lock(comm->m_endpoint.ft_mutex);
        comm->m_endpoint.ft_any_comm_requests.emplace(req);
    } else {
        if (comm->inserted == false) {
            std::unique_lock end_lock(comm->m_endpoint.ft_mutex);
            comm->m_endpoint.ft_comms.push_back(comm);
            debug_info("ft_comms size " << comm->m_endpoint.ft_comms.size);
        }
    }

    comm->ft_requests.push_back(req);
    debug_info("[LFI] emplace request " << std::hex << req << std::dec << " in comm " << comm->rank_peer
                                        << " ft_requests size " << comm->ft_requests.size);
    debug_info(*req);
}

void lfi_ft_manager::on_request_complete(lfi_request *req, int err) {
    const auto comm = req->m_comm;
    if (comm) {
        std::unique_lock ft_lock(comm->ft_mutex);
        comm->ft_requests.erase(req);
        debug_info("[LFI] erase request " << std::hex << req << std::dec << " in comm " << comm->rank_peer
                                          << " ft_requests size " << comm->ft_requests.size);
        debug_info(*req);
        if (req->m_comm_id == ANY_COMM_SHM || req->m_comm_id == ANY_COMM_PEER) {
            std::unique_lock end_lock(comm->m_endpoint.ft_mutex);
            req->m_endpoint.ft_any_comm_requests.erase(req);
        }
        if (err == LFI_SUCCESS) {
            lfi_comm *comm_to_update = req->m_comm;
            std::optional<std::shared_lock<std::shared_mutex>> opt_lock;
            if (req->source != req->m_comm_id) {
                ft_lock.unlock();
                opt_lock.emplace(m_lfi.m_comms_mutex);
                comm_to_update = m_lfi.get_comm_internal(*opt_lock, req->source);
                ft_lock.lock();
            }
            if (comm_to_update) {
                comm_to_update->ft_last_request_time = lfi_comm::clock::now();
            }
        }
    }
    // end = lfi_comm::clock::now();

    // auto ellapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    // auto ellapsed_ft_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ft - start).count();
    // print("on_request_complete " << ellapsed_ft_ns << "ns " << ellapsed_ns << "ns");
}

void lfi_ft_manager::process_comm(lfi_comm *comm, int32_t ft_ms, std::vector<uint32_t> &canceled_coms,
                                  std::chrono::time_point<std::chrono::high_resolution_clock> now) {
    {
        std::unique_lock ft_lock(comm->ft_mutex);
        if (comm->ft_requests.size == 0) {
            std::unique_lock end_lock(comm->m_endpoint.ft_mutex);
            comm->m_endpoint.ft_comms.erase(comm);
            return;
        }
    }
    if (comm->ft_current_status == lfi_comm::ft_status::IDLE) {
        auto last_request_time = comm->ft_last_request_time;
        int32_t elapsed_ms_req = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_request_time).count();
        if (elapsed_ms_req >= ft_ms) {
            if (!comm->ft_heartbeat_req) {
                comm->ft_heartbeat_req = std::make_unique<lfi_request>(comm->m_endpoint, comm);
            }
            if (comm->ft_heartbeat_req->is_completed()) {
                comm->ft_heartbeat_req->reset();
                comm->ft_value_heartbeat = 0;
                debug_info("Get hearbeat comm " << comm->rank_peer);
                int ret = m_lfi.async_get(&comm->ft_value_heartbeat, sizeof(comm->ft_value_heartbeat),
                                          comm->ft_remote_heartbeat_addr, comm->ft_remote_heartbeat_key,
                                          *comm->ft_heartbeat_req, true);
                if (ret < 0) {
                    canceled_coms.emplace_back(comm->rank_peer);
                    comm->ft_current_status = lfi_comm::ft_status::ERROR;
                    return;
                }

                comm->ft_current_status = lfi_comm::ft_status::HEARTBEAT;
                comm->ft_heartbeat_time_point = std::chrono::high_resolution_clock::now();
            }
        }
    } else if (comm->ft_current_status == lfi_comm::ft_status::HEARTBEAT) {
        std::unique_lock lock(comm->ft_heartbeat_req->mutex);
        if (comm->ft_heartbeat_req && comm->ft_heartbeat_req->is_completed() &&
            comm->ft_value_heartbeat == HEARBEAT_CODE) {
            debug_info("Hearbeat comm " << comm->rank_peer << " code 0x" << std::hex << comm->ft_value_heartbeat
                                        << std::dec);
            comm->ft_current_status = lfi_comm::ft_status::IDLE;
        } else {
            int32_t elapsed_ms_pp =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - comm->ft_heartbeat_time_point).count();
            if (elapsed_ms_pp > std::max(ft_ms, 1000)) {
                canceled_coms.emplace_back(comm->rank_peer);
                comm->ft_current_status = lfi_comm::ft_status::ERROR;
            }
        }
    }
}

void lfi_ft_manager::handle_any_comm_reports(lfi_endpoint &lfi_ep, std::vector<uint32_t> &canceled_coms) {
    {
        std::unique_lock ft_ep_lock(lfi_ep.ft_mutex);
        if (lfi_ep.ft_any_comm_requests.empty() && lfi_ep.ft_pending_failed_comms.empty() && canceled_coms.empty())
            return;

        lfi_ep.ft_requests_to_cancel.reserve(10);

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
                lfi_ep.ft_requests_to_cancel.push_back(any_req);

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

    for (auto req : lfi_ep.ft_requests_to_cancel) {
        req->cancel();
    }
    lfi_ep.ft_requests_to_cancel.clear();
}

void lfi_ft_manager::one_loop(lfi_endpoint &lfi_ep) {
    LFI_PROFILE_FUNCTION();

    lfi_ep.ft_progress_count++;
    if (lfi_ep.ft_progress_count < 100) {
        return;
    } else {
        lfi_ep.ft_progress_count = 0;
    }

    auto now = std::chrono::high_resolution_clock::now();

    // Only run once each 10 ms
    auto ellapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - lfi_ep.ft_last_progress).count();
    if (ellapsed_ms < 10) {
        return;
    }

    lfi_ep.ft_last_progress = now;
    lfi_ep.ft_canceled_coms.reserve(10);
    int32_t ft_ms = std::max(0, env::get_instance().LFI_fault_tolerance_time * 1000);
    bool is_any_comm = false;
    {
        std::shared_lock comm_lock(m_lfi.m_comms_mutex);
        {
            std::unique_lock ft_ep_lock(lfi_ep.ft_mutex);

            if (lfi_ep.ft_any_comm_requests.size() > 0) {
                ft_ep_lock.unlock();
                is_any_comm = true;
                for (auto &&[comm_id, comm] : m_lfi.m_comms) {
                    if (comm && comm->m_endpoint == lfi_ep && comm->rank_peer != ANY_COMM_SHM &&
                        comm->rank_peer != ANY_COMM_PEER) {
                        process_comm(comm.get(), ft_ms, lfi_ep.ft_canceled_coms, now);
                    }
                }
            } else {
                {
                    auto comm = lfi_ep.ft_comms.head;
                    while (comm) {
                        if (comm == nullptr) break;
                        ft_ep_lock.unlock();
                        process_comm(comm, ft_ms, lfi_ep.ft_canceled_coms, now);
                        ft_ep_lock.lock();
                        comm = comm->next;
                    }
                }
            }
        }

        for (auto &&comm_id : lfi_ep.ft_canceled_coms) {
            auto comm_ptr = m_lfi.get_comm_internal(comm_lock, comm_id);
            if (comm_ptr) cancel_comm(*comm_ptr);
        }
    }

    if (is_any_comm) handle_any_comm_reports(lfi_ep, lfi_ep.ft_canceled_coms);
    lfi_ep.ft_canceled_coms.clear();
}

int lfi_ft_manager::cancel_comm(lfi_comm &comm) {
    LFI_PROFILE_FUNCTION();
    {
        std::unique_lock lock(comm.ft_mutex);
        comm.is_canceled = true;
        debug_info("[LFI] Canceling comm with rank " << comm.rank_peer);
        auto request = comm.ft_requests.head;
        while (request) {
            if (request == nullptr) break;
            lock.unlock();
            request->cancel();
            lock.lock();

            request = comm.ft_requests.head;
        }
    }

    comm.ft_heartbeat_req = nullptr;
    {
        std::unique_lock ft_lock(comm.m_endpoint.ft_mutex);
        comm.m_endpoint.ft_comms.erase(&comm);
    }
    return 0;
}

}  // namespace LFI
