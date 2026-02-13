
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

#include <rdma/fi_rma.h>

#include "helpers.hpp"
#include "impl/debug.hpp"
#include "impl/ft_manager.hpp"
#include "impl/lfi.hpp"
#include "lfi_request.hpp"

namespace LFI {

struct format_fi_flags {
    uint64_t flags;

    friend std::ostream &operator<<(std::ostream &os, const format_fi_flags &format) {
        if (format.flags == 0) os << "    NONE" << std::endl;
        if (format.flags & FI_MSG) os << "    FI_MSG" << std::endl;
        if (format.flags & FI_RMA) os << "    FI_RMA" << std::endl;
        if (format.flags & FI_TAGGED) os << "    FI_TAGGED" << std::endl;
        if (format.flags & FI_ATOMIC) os << "    FI_ATOMIC" << std::endl;
        if (format.flags & FI_MULTICAST) os << "    FI_MULTICAST" << std::endl;
        if (format.flags & FI_COLLECTIVE) os << "    FI_COLLECTIVE" << std::endl;

        if (format.flags & FI_READ) os << "    FI_READ" << std::endl;
        if (format.flags & FI_WRITE) os << "    FI_WRITE" << std::endl;
        if (format.flags & FI_RECV) os << "    FI_RECV" << std::endl;
        if (format.flags & FI_SEND) os << "    FI_SEND" << std::endl;
        if (format.flags & FI_REMOTE_READ) os << "    FI_REMOTE_READ" << std::endl;
        if (format.flags & FI_REMOTE_WRITE) os << "    FI_REMOTE_WRITE" << std::endl;

        if (format.flags & FI_MULTI_RECV) os << "    FI_MULTI_RECV" << std::endl;
        if (format.flags & FI_REMOTE_CQ_DATA) os << "    FI_REMOTE_CQ_DATA" << std::endl;
        if (format.flags & FI_MORE) os << "    FI_MORE" << std::endl;
        if (format.flags & FI_PEEK) os << "    FI_PEEK" << std::endl;
        if (format.flags & FI_TRIGGER) os << "    FI_TRIGGER" << std::endl;
        if (format.flags & FI_FENCE) os << "    FI_FENCE" << std::endl;
        // if (flags & FI_PRIORITY) os << "    FI_PRIORITY" << std::endl;

        if (format.flags & FI_COMPLETION) os << "    FI_COMPLETION" << std::endl;
        if (format.flags & FI_INJECT) os << "    FI_INJECT" << std::endl;
        if (format.flags & FI_INJECT_COMPLETE) os << "    FI_INJECT_COMPLETE" << std::endl;
        if (format.flags & FI_TRANSMIT_COMPLETE) os << "    FI_TRANSMIT_COMPLETE" << std::endl;
        if (format.flags & FI_DELIVERY_COMPLETE) os << "    FI_DELIVERY_COMPLETE" << std::endl;
        if (format.flags & FI_AFFINITY) os << "    FI_AFFINITY" << std::endl;
        if (format.flags & FI_COMMIT_COMPLETE) os << "    FI_COMMIT_COMPLETE" << std::endl;
        if (format.flags & FI_MATCH_COMPLETE) os << "    FI_MATCH_COMPLETE" << std::endl;

        if (format.flags & FI_HMEM) os << "    FI_HMEM" << std::endl;
        if (format.flags & FI_VARIABLE_MSG) os << "    FI_VARIABLE_MSG" << std::endl;
        if (format.flags & FI_RMA_PMEM) os << "    FI_RMA_PMEM" << std::endl;
        if (format.flags & FI_SOURCE_ERR) os << "    FI_SOURCE_ERR" << std::endl;
        if (format.flags & FI_LOCAL_COMM) os << "    FI_LOCAL_COMM" << std::endl;
        if (format.flags & FI_REMOTE_COMM) os << "    FI_REMOTE_COMM" << std::endl;
        if (format.flags & FI_SHARED_AV) os << "    FI_SHARED_AV" << std::endl;
        if (format.flags & FI_PROV_ATTR_ONLY) os << "    FI_PROV_ATTR_ONLY" << std::endl;
        if (format.flags & FI_NUMERICHOST) os << "    FI_NUMERICHOST" << std::endl;
        if (format.flags & FI_RMA_EVENT) os << "    FI_RMA_EVENT" << std::endl;
        if (format.flags & FI_SOURCE) os << "    FI_SOURCE" << std::endl;
        if (format.flags & FI_NAMED_RX_CTX) os << "    FI_NAMED_RX_CTX" << std::endl;
        if (format.flags & FI_DIRECTED_RECV) os << "    FI_DIRECTED_RECV" << std::endl;
        return os;
    }
};

std::ostream &operator<<(std::ostream &os, const fi_cq_tagged_entry &entry) {
    os << "fi_cq_tagged_entry:" << std::endl;
    os << "  op_context: " << entry.op_context << std::endl;
    os << "  Flags set:" << std::endl;
    os << format_fi_flags{entry.flags};
    os << "  len: " << entry.len << std::endl;
    os << "  buf: " << entry.buf << std::endl;
    os << "  data: " << entry.data << std::endl;
    os << "  tag: " << entry.tag << std::endl;
    if (entry.flags & FI_RECV) {
        os << "    real_tag: " << format_lfi_tag{entry.tag & MASK_TAG} << std::endl;
        os << "    rank: " << ((entry.tag & MASK_RANK) >> MASK_RANK_BYTES) << std::endl;
    }
    return os;
}

struct format_fi_cq_err_entry {
    const fi_cq_err_entry &entry;
    fid_cq *cq;

    explicit format_fi_cq_err_entry(const fi_cq_err_entry &entry, fid_cq *cq) : entry(entry), cq(cq) {}

    friend std::ostream &operator<<(std::ostream &os, const format_fi_cq_err_entry &format) {
        os << "fi_cq_err_entry:" << std::endl;
        os << "  op_context: " << format.entry.op_context << std::endl;
        os << "  Flags set:" << std::endl;
        os << format_fi_flags{format.entry.flags};
        os << "  len: " << format.entry.len << std::endl;
        os << "  buf: " << format.entry.buf << std::endl;
        os << "  data: " << format.entry.data << std::endl;
        os << "  tag: " << format.entry.tag << std::endl;
        os << "    real_tag: " << format_lfi_tag{format.entry.tag & MASK_TAG} << std::endl;
        os << "    rank: " << ((format.entry.tag & MASK_RANK) >> MASK_RANK_BYTES) << std::endl;
        os << "  olen: " << format.entry.olen << std::endl;
        os << "  err: " << format.entry.err << " " << fi_strerror(format.entry.err) << std::endl;
        os << "  prov_errno: " << format.entry.prov_errno << " "
           << fi_cq_strerror(format.cq, format.entry.prov_errno, format.entry.err_data, NULL, 0) << std::endl;
        os << "  err_data: " << format.entry.err_data << std::endl;
        os << "  err_data_size: " << format.entry.err_data_size << std::endl;
        return os;
    }
};

void lfi_endpoint::post_pending_ops() {
    {
        std::unique_lock<std::mutex> lock(pending_ops_mutex);
        auto process_queue = [&](VectorQueue<lfi_pending_op> &queue) {
            while (!queue.empty()) {
                auto &op = queue.front();
                int op_ret = 0;
                switch (op.type) {
                    case lfi_pending_op::Type::NONE:
                        op_ret = 0;
                        debug_error("[LFI] Pending op NONE");
                        break;
                    case lfi_pending_op::Type::SEND:
                        op_ret = fi_tsend(op.ep, op.buf.cbuf, op.len, nullptr, op.addr, op.tag_remote_addr, op.context);
                        break;
                    case lfi_pending_op::Type::SENDV:
                        op_ret = fi_tsendv(op.ep, static_cast<const iovec *>(op.buf.cbuf), nullptr, op.len, op.addr,
                                           op.tag_remote_addr, op.context);
                        break;
                    case lfi_pending_op::Type::RECV:
                        op_ret = fi_trecv(op.ep, op.buf.buf, op.len, nullptr, op.addr, op.tag_remote_addr,
                                          op.ignore_remote_key, op.context);
                        break;
                    case lfi_pending_op::Type::RECVV:
                        op_ret = fi_trecvv(op.ep, static_cast<const iovec *>(op.buf.cbuf), nullptr, op.len, op.addr,
                                           op.tag_remote_addr, op.ignore_remote_key, op.context);
                        break;
                    case lfi_pending_op::Type::INJECT:
                        op_ret = fi_tinject(op.ep, op.buf.cbuf, op.len, op.addr, op.tag_remote_addr);
                        break;
                    case lfi_pending_op::Type::PUT:
                        op_ret = fi_write(op.ep, op.buf.cbuf, op.len, nullptr, op.addr, op.tag_remote_addr,
                                          op.ignore_remote_key, op.context);
                        break;
                    case lfi_pending_op::Type::GET:
                        op_ret = fi_read(op.ep, op.buf.buf, op.len, nullptr, op.addr, op.tag_remote_addr,
                                         op.ignore_remote_key, op.context);
                        break;
                }

                if (op_ret == -FI_EAGAIN) {
                    debug_info("[LFI] Pending op FI_EAGAIN, remaining ops: " << queue.size());
                    debug_info("[LFI] op: " << op);
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
                    } else if (op.type == lfi_pending_op::Type::INJECT) {
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

                    debug_info("[LFI] Pending op posted, remaining " << queue.size() - 1);
                    debug_info("[LFI] op: " << op);
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
            debug_info(format_fi_cq_err_entry(err, this->cq));

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
                    print(format_fi_cq_err_entry(err, this->cq));
                    error = -LFI_LIBFABRIC_ERROR;
                }
                request->complete(error);
            }
            m_lfi.req_ctx_factory.destroy(ctx);
        } else if (ret > 0) {
            // Handle the cq entries
            for (int i = 0; i < ret; i++) {
                lfi_request_context *ctx = static_cast<lfi_request_context *>(comp[i].op_context);
                debug_info(comp[i]);
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
                    if (request->op_type == lfi_request::OpType::RECV) {
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
    } while (ret == MAX_COMP_COUNT || ret == -FI_EAVAIL);

    post_pending_ops();

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