
#include "impl/ft_manager.hpp"

#include "impl/debug.hpp"
#include "impl/env.hpp"
#include "impl/lfi.hpp"
#include "impl/profiler.hpp"
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
    if (m_heartbeat_key >= 0) {
        m_lfi.mr_unreg(m_heartbeat_key);
        m_heartbeat_key = -1;
    }
    debug_info("[LFI][FT] Manager End Stop");
}

void lfi_ft_manager::setup_heartbeat() {
    LFI_PROFILE_FUNCTION();
    if (m_heartbeat_key >= 0) return;
    m_heartbeat_key = m_lfi.mr_reg(&m_local_heartbeat, sizeof(m_local_heartbeat));
    if (m_heartbeat_key < 0) {
        print_error("Error registering local heartbeat MR");
    }
}

void lfi_ft_manager::register_request(lfi_request *req, lfi_comm *comm) {
    if (!env::get_instance().LFI_fault_tolerance) return;

    std::scoped_lock ft_lock(comm->m_endpoint.ft_mutex, comm->ft_mutex);
    if (comm->rank_peer == ANY_COMM_SHM || comm->rank_peer == ANY_COMM_PEER) {
        debug_info("Save request in any_comm_requests " << req);
        comm->m_endpoint.ft_any_comm_requests.emplace(req);
    } else {
        if (comm->ft_requests.size() == 0) {
            comm->m_endpoint.ft_comms.emplace(comm);
        }
    }

    comm->ft_requests.emplace(req);
    debug_info("[LFI] emplace request " << std::hex << req << std::dec << " in comm " << comm->rank_peer
                                        << " ft_requests size " << comm->ft_requests.size());
    debug_info(*req);
}

void lfi_ft_manager::on_request_complete(lfi_request *req, int &err) {
    auto [lock, comm] = m_lfi.get_comm_and_mutex(req->m_comm_id);
    if (comm) {
        std::scoped_lock ft_lock(comm->m_endpoint.ft_mutex, comm->ft_mutex);
        comm->ft_requests.erase(req);
        if (req->m_comm_id != ANY_COMM_SHM && req->m_comm_id != ANY_COMM_PEER) {
            if (comm->ft_requests.size() == 0) {
                req->m_endpoint.ft_comms.erase(comm);
            }
        } else {
            req->m_endpoint.ft_any_comm_requests.erase(req);
        }
    }
    if (err == LFI_SUCCESS) {
        auto comm_ptr = m_lfi.get_comm_internal(lock, req->source);
        if (comm_ptr) {
            std::unique_lock ft_lock(comm_ptr->ft_mutex);
            comm_ptr->ft_last_request_time = lfi_comm::clock::now();
        }
    }
}

void lfi_ft_manager::process_comm(lfi_comm *comm, int32_t ft_ms, std::vector<uint32_t> &canceled_coms,
                                  std::chrono::time_point<std::chrono::high_resolution_clock> now) {
    if (comm->ft_current_status == lfi_comm::ft_status::IDLE) {
        decltype(comm->ft_last_request_time) last_request_time;
        {
            std::unique_lock ft_lock(comm->ft_mutex);
            last_request_time = comm->ft_last_request_time;
        }
        int32_t elapsed_ms_req = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_request_time).count();
        if (elapsed_ms_req > ft_ms) {
            if (!comm->ft_heartbeat_req) {
                comm->ft_heartbeat_req = std::make_unique<lfi_request>(comm->m_endpoint, comm->rank_peer);
            }
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
    } else if (comm->ft_current_status == lfi_comm::ft_status::HEARTBEAT) {
        std::unique_lock lock(comm->ft_heartbeat_req->mutex);
        if (comm->ft_heartbeat_req->is_completed() && comm->ft_value_heartbeat == HEARBEAT_CODE) {
            debug_info("Hearbeat comm " << comm->rank_peer << " code " << std::hex << comm->ft_value_heartbeat
                                        << std::dec);
            comm->ft_current_status = lfi_comm::ft_status::IDLE;
        } else {
            int32_t elapsed_ms_pp =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - comm->ft_heartbeat_time_point).count();
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

    auto now = std::chrono::high_resolution_clock::now();

    // Only run once each 10 ms
    auto ellapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - lfi_ep.ft_last_progress).count();
    if (ellapsed_ms < 10) {
        return;
    }

    lfi_ep.ft_last_progress = now;
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
                    process_comm(comm.get(), ft_ms, m_canceled_coms, now);
                }
            }
        } else {
            std::unordered_set<lfi_comm *> temp_ft_comms(lfi_ep.ft_comms);
            ft_ep_lock.unlock();
            for (auto &&comm : temp_ft_comms) {
                process_comm(comm, ft_ms, m_canceled_coms, now);
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
