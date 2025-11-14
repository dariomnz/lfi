
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

#include "impl/lfi_endpoint.hpp"

#include "impl/debug.hpp"
#include "impl/env.hpp"
#include "impl/lfi.hpp"
#include "sstream"

namespace LFI {

inline std::string fi_flags_to_string(uint64_t flags) {
    std::stringstream out;
    if (flags == 0) out << "    NONE" << std::endl;
    if (flags & FI_MSG) out << "    FI_MSG" << std::endl;
    if (flags & FI_RMA) out << "    FI_RMA" << std::endl;
    if (flags & FI_TAGGED) out << "    FI_TAGGED" << std::endl;
    if (flags & FI_ATOMIC) out << "    FI_ATOMIC" << std::endl;
    if (flags & FI_MULTICAST) out << "    FI_MULTICAST" << std::endl;
    if (flags & FI_COLLECTIVE) out << "    FI_COLLECTIVE" << std::endl;

    if (flags & FI_READ) out << "    FI_READ" << std::endl;
    if (flags & FI_WRITE) out << "    FI_WRITE" << std::endl;
    if (flags & FI_RECV) out << "    FI_RECV" << std::endl;
    if (flags & FI_SEND) out << "    FI_SEND" << std::endl;
    if (flags & FI_REMOTE_READ) out << "    FI_REMOTE_READ" << std::endl;
    if (flags & FI_REMOTE_WRITE) out << "    FI_REMOTE_WRITE" << std::endl;

    if (flags & FI_MULTI_RECV) out << "    FI_MULTI_RECV" << std::endl;
    if (flags & FI_REMOTE_CQ_DATA) out << "    FI_REMOTE_CQ_DATA" << std::endl;
    if (flags & FI_MORE) out << "    FI_MORE" << std::endl;
    if (flags & FI_PEEK) out << "    FI_PEEK" << std::endl;
    if (flags & FI_TRIGGER) out << "    FI_TRIGGER" << std::endl;
    if (flags & FI_FENCE) out << "    FI_FENCE" << std::endl;
    // if (flags & FI_PRIORITY) out << "    FI_PRIORITY" << std::endl;

    if (flags & FI_COMPLETION) out << "    FI_COMPLETION" << std::endl;
    if (flags & FI_INJECT) out << "    FI_INJECT" << std::endl;
    if (flags & FI_INJECT_COMPLETE) out << "    FI_INJECT_COMPLETE" << std::endl;
    if (flags & FI_TRANSMIT_COMPLETE) out << "    FI_TRANSMIT_COMPLETE" << std::endl;
    if (flags & FI_DELIVERY_COMPLETE) out << "    FI_DELIVERY_COMPLETE" << std::endl;
    if (flags & FI_AFFINITY) out << "    FI_AFFINITY" << std::endl;
    if (flags & FI_COMMIT_COMPLETE) out << "    FI_COMMIT_COMPLETE" << std::endl;
    if (flags & FI_MATCH_COMPLETE) out << "    FI_MATCH_COMPLETE" << std::endl;

    if (flags & FI_HMEM) out << "    FI_HMEM" << std::endl;
    if (flags & FI_VARIABLE_MSG) out << "    FI_VARIABLE_MSG" << std::endl;
    if (flags & FI_RMA_PMEM) out << "    FI_RMA_PMEM" << std::endl;
    if (flags & FI_SOURCE_ERR) out << "    FI_SOURCE_ERR" << std::endl;
    if (flags & FI_LOCAL_COMM) out << "    FI_LOCAL_COMM" << std::endl;
    if (flags & FI_REMOTE_COMM) out << "    FI_REMOTE_COMM" << std::endl;
    if (flags & FI_SHARED_AV) out << "    FI_SHARED_AV" << std::endl;
    if (flags & FI_PROV_ATTR_ONLY) out << "    FI_PROV_ATTR_ONLY" << std::endl;
    if (flags & FI_NUMERICHOST) out << "    FI_NUMERICHOST" << std::endl;
    if (flags & FI_RMA_EVENT) out << "    FI_RMA_EVENT" << std::endl;
    if (flags & FI_SOURCE) out << "    FI_SOURCE" << std::endl;
    if (flags & FI_NAMED_RX_CTX) out << "    FI_NAMED_RX_CTX" << std::endl;
    if (flags & FI_DIRECTED_RECV) out << "    FI_DIRECTED_RECV" << std::endl;
    return out.str();
}

inline std::string fi_cq_tagged_entry_to_string(const fi_cq_tagged_entry &entry) {
    std::stringstream out;
    out << "fi_cq_tagged_entry:" << std::endl;
    out << "  op_context: " << entry.op_context << std::endl;
    out << "  Flags set:" << std::endl;
    out << fi_flags_to_string(entry.flags);
    out << "  len: " << entry.len << std::endl;
    out << "  buf: " << entry.buf << std::endl;
    out << "  data: " << entry.data << std::endl;
    out << "  tag: " << entry.tag << std::endl;
    if (entry.flags & FI_RECV) {
        // out << "    real_tag: " << (entry.tag & MASK_TAG) << std::endl;
        out << "    real_tag: " << lfi_tag_to_string(entry.tag & MASK_TAG) << std::endl;
        out << "    rank: " << ((entry.tag & MASK_RANK) >> MASK_RANK_BYTES) << std::endl;
    }
    return out.str();
}

inline std::string fi_cq_err_entry_to_string(const fi_cq_err_entry &entry, fid_cq *cq) {
    std::stringstream out;
    out << "fi_cq_err_entry:" << std::endl;
    out << "  op_context: " << entry.op_context << std::endl;
    out << "  Flags set:" << std::endl;
    out << fi_flags_to_string(entry.flags);
    out << "  len: " << entry.len << std::endl;
    out << "  buf: " << entry.buf << std::endl;
    out << "  data: " << entry.data << std::endl;
    out << "  tag: " << entry.tag << std::endl;
    out << "    real_tag: " << (entry.tag & MASK_TAG) << std::endl;
    out << "    rank: " << ((entry.tag & MASK_RANK) >> MASK_RANK_BYTES) << std::endl;
    out << "  olen: " << entry.olen << std::endl;
    out << "  err: " << entry.err << " " << fi_strerror(entry.err) << std::endl;
    out << "  prov_errno: " << entry.prov_errno << " " << fi_cq_strerror(cq, entry.prov_errno, entry.err_data, NULL, 0)
        << std::endl;
    out << "  err_data: " << entry.err_data << std::endl;
    out << "  err_data_size: " << entry.err_data_size << std::endl;
    return out.str();
}

int lfi_endpoint::progress() {
    int ret = 0;
    const int MAX_COMP_COUNT = 8;
    struct fi_cq_tagged_entry comp[MAX_COMP_COUNT] = {};

    // Do while in cause the cp have more msgs to report
    do {
        if (ret == MAX_COMP_COUNT) {
            debug_info("[LFI] fi_cq_read full. Retrying.");
        }
        if (ret == -FI_EAVAIL) {
            debug_info("[LFI] fi_cq_read EAVAIL. Retrying.");
        }
        // Libfabric progress
        ret = fi_cq_read(this->cq, comp, MAX_COMP_COUNT);
        if (ret == -FI_EAGAIN) {
            ret = LFI_SUCCESS;
        } else if (ret == -FI_EAVAIL) {
            debug_info("[Error] fi_cq_read " << ret << " " << fi_strerror(ret));
            fi_cq_err_entry err;
            fi_cq_readerr(this->cq, &err, 0);
            debug_info(fi_cq_err_entry_to_string(err, this->cq));

            lfi_request_context *ctx = static_cast<lfi_request_context *>(err.op_context);

            lfi_request *request = ctx->get_request();
            if (request) {
                {
#ifdef DEBUG
                    std::unique_lock request_lock(request->mutex);
                    debug_info("[Error] " << *request << " cq error");
#endif
                }
                int error;
                if (std::abs(err.err) == FI_ECANCELED) {
                    error = -LFI_CANCELED;
                } else if (std::abs(err.err) == FI_ENOMSG) {
                    error = -LFI_PEEK_NO_MSG;
                } else {
                    error = -LFI_LIBFABRIC_ERROR;
                }
                request->complete(error);
            }
            m_lfi.req_ctx_factory.destroy(ctx);
        } else if (ret > 0) {
            // Handle the cq entries
            for (int i = 0; i < ret; i++) {
                lfi_request_context *ctx = static_cast<lfi_request_context *>(comp[i].op_context);
                debug_info(fi_cq_tagged_entry_to_string(comp[i]));
                lfi_request *request = ctx->get_request();
                if (!request) {
                    print(fi_cq_tagged_entry_to_string(comp[i]));
                    print("[LFI] [ERROR] internal error, context without request");
                    m_lfi.req_ctx_factory.destroy(ctx);
                    continue;
                }
                uint32_t comm_id = UNINITIALIZED_COMM;
                uint32_t source = UNINITIALIZED_COMM;
                {
                    std::unique_lock request_lock(request->mutex);
                    // If len is not 0 is a RECV
                    if (!request->is_send) {
                        request->size = comp[i].len;
                        request->tag = (comp[i].tag & MASK_TAG);
                        request->source = ((comp[i].tag & MASK_RANK) >> MASK_RANK_BYTES);

                        if (env::get_instance().LFI_fault_tolerance) {
                            comm_id = request->m_comm.rank_peer;
                            source = request->source;
                        }
                    }
                    debug_info("Completed: " << *request);
                }
                if (source != UNINITIALIZED_COMM) {
                    // Update time outside request lock
                    if (comm_id == ANY_COMM_SHM || comm_id == ANY_COMM_PEER) {
                        auto comm = m_lfi.get_comm(source);
                        if (comm) {
                            comm->update_request_time();
                        }
                    } else {
                        auto comm = m_lfi.get_comm(comm_id);
                        if (comm) {
                            comm->update_request_time();
                        }
                    }
                }
                request->complete(LFI_SUCCESS);
                m_lfi.req_ctx_factory.destroy(ctx);
            }
        }
    } while (ret == MAX_COMP_COUNT || ret == -FI_EAVAIL);

    if (env::get_instance().LFI_fault_tolerance) {
        static auto last_ft_execution_time = std::chrono::high_resolution_clock::now();
        static std::mutex exclusive_mutex{};
        std::unique_lock lock(exclusive_mutex, std::defer_lock);
        if (lock.try_lock()) {
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed_time =
                std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_ft_execution_time);

            if (elapsed_time > std::chrono::milliseconds(10)) {
                // debug_info("[LFI] runing ft");
                m_lfi.ft_one_loop(*this);
                last_ft_execution_time = current_time;
            }
        }
    }

    return ret;
}

bool lfi_endpoint::protected_progress() {
    bool made_progress = false;
    if (!env::get_instance().LFI_efficient_progress || !in_progress.exchange(true)) {
        // debug_info("Run progress from protected_progress");
        progress();
        in_progress.store(false);
        made_progress = true;
    }
    return made_progress;
}
}  // namespace LFI