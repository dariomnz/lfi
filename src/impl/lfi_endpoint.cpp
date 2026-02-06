
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

#include "helpers.hpp"
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

void lfi_endpoint::post_pending_ops() {
    {
        std::unique_lock<std::mutex> lock(pending_ops_mutex);

        auto process_queue = [&](VectorQueue<PendingOp> &queue) {
            while (!queue.empty()) {
                auto &op = queue.front();
                int op_ret = 0;
                switch (op.type) {
                    case PendingOp::Type::SEND:
                        op_ret = fi_tsend(op.ep, op.buf.cbuf, op.len, nullptr, op.addr, op.tag, op.context);
                        break;
                    case PendingOp::Type::SENDV:
                        op_ret = fi_tsendv(op.ep, static_cast<const iovec *>(op.buf.cbuf), nullptr, op.len, op.addr,
                                           op.tag, op.context);
                        break;
                    case PendingOp::Type::RECV:
                        op_ret = fi_trecv(op.ep, op.buf.buf, op.len, nullptr, op.addr, op.tag, op.ignore, op.context);
                        break;
                    case PendingOp::Type::RECVV:
                        op_ret = fi_trecvv(op.ep, static_cast<const iovec *>(op.buf.cbuf), nullptr, op.len, op.addr,
                                           op.tag, op.ignore, op.context);
                        break;
                    case PendingOp::Type::INJECT:
                        op_ret = fi_tinject(op.ep, op.buf.cbuf, op.len, op.addr, op.tag);
                        break;
                }

                if (op_ret == -FI_EAGAIN) {
                    // debug_info("[LFI] Pending op FI_EAGAIN");
                    // Not enough resources to post the operation, the queue is not empty
                    return false;
                } else {
                    if (op_ret != 0) {
                        debug_info("[LFI] Error in pending op: " << fi_strerror(op_ret));
                        if (op.context) {
                            lfi_request_context *ctx = static_cast<lfi_request_context *>(op.context);
                            if (ctx) {
                                lfi_request *req = ctx->get_request();
                                if (req) {
                                    req->complete(-LFI_LIBFABRIC_ERROR);
                                }
                            }
                        }
                    } else if (op.type == PendingOp::Type::INJECT) {
                        // Inject does not generate CQ entry, complete it here
                        if (op.context) {
                            lfi_request_context *ctx = static_cast<lfi_request_context *>(op.context);
                            if (ctx) {
                                lfi_request *req = ctx->get_request();
                                if (req) {
                                    req->complete(LFI_SUCCESS);
                                }
                                m_lfi.req_ctx_factory.destroy(ctx);
                            }
                        }
                    }

                    debug_info("[LFI] Pending op posted, remaining " << queue.size());
                    queue.pop();
                }
            }
            // Finish processing queue
            return true;
        };

        if (process_queue(priority_ops)) {
            process_queue(pending_ops);
        }
    }
}

int lfi_endpoint::progress(bool call_callbacks) {
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
                } else if (std::abs(err.err) == FI_ETRUNC) {
                    error = -LFI_ETRUN_RECV;
                } else {
                    print(fi_strerror(err.err));
                    print(fi_cq_err_entry_to_string(err, this->cq));
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
                    // debug_info(fi_cq_tagged_entry_to_string(comp[i]));
                    debug_info("[LFI] [ERROR] internal error, context without request");
                    m_lfi.req_ctx_factory.destroy(ctx);
                    continue;
                }
                {
                    std::unique_lock request_lock(request->mutex);
                    // If len is not 0 is a RECV
                    if (!request->is_send) {
                        request->size = comp[i].len;
                        request->tag = (comp[i].tag & MASK_TAG);
                        request->source = ((comp[i].tag & MASK_RANK) >> MASK_RANK_BYTES);
                    }
                    debug_info("Completed: " << *request);
                }
                request->complete(LFI_SUCCESS);
                m_lfi.req_ctx_factory.destroy(ctx);
            }
        }
        if (call_callbacks) {
            std::vector<std::tuple<lfi_request_callback, int, void *>> swap_callbacks;
            {
                std::unique_lock callback_lock(callbacks_mutex);
                callbacks.swap(swap_callbacks);
            }
            for (auto &&[callback, error, ctx] : swap_callbacks) {
                debug_info("[LFI] calling callbacks");
                callback(error, ctx);
            }
        }
    } while (ret == MAX_COMP_COUNT || ret == -FI_EAVAIL);

    post_pending_ops();

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

int lfi_endpoint::protected_progress(bool call_callbacks) {
    // LFI_PROFILE_FUNCTION();
    int made_progress = -1;
    ProgressGuard guard(*this);
    if (guard.is_leader()) {
        // debug_info("Run progress from protected_progress");
        made_progress = progress(call_callbacks);
    }

    return made_progress;
}
}  // namespace LFI