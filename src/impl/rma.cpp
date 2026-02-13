
#include "impl/debug.hpp"
#include "impl/ft_manager.hpp"
#include "impl/lfi.hpp"
#include "impl/profiler.hpp"

namespace LFI {

int LFI::put(uint32_t comm_id, const void *buffer, size_t size, uint64_t remote_addr, uint64_t remote_key) {
    LFI_PROFILE_FUNCTION();
    int ret = 0;
    debug_info("[LFI] Start");

    // Check if comm exists
    auto [lock, comm] = get_comm_and_mutex(comm_id);
    if (!comm) {
        return -LFI_COMM_NOT_FOUND;
    }
    lfi_request request(comm->m_endpoint, comm->rank_peer);
    lock.unlock();

    ret = async_put(buffer, size, remote_addr, remote_key, request);

    if (ret < 0) {
        return ret;
    }

    wait(request);

    debug_info("[LFI] End");
    if (request.error < 0) {
        return request.error;
    }
    return request.size;
}

int LFI::get(uint32_t comm_id, void *buffer, size_t size, uint64_t remote_addr, uint64_t remote_key) {
    LFI_PROFILE_FUNCTION();
    int ret = 0;
    debug_info("[LFI] Start");

    // Check if comm exists
    auto [lock, comm] = get_comm_and_mutex(comm_id);
    if (!comm) {
        return -LFI_COMM_NOT_FOUND;
    }
    lfi_request request(comm->m_endpoint, comm->rank_peer);
    lock.unlock();

    ret = async_get(buffer, size, remote_addr, remote_key, request);

    if (ret < 0) {
        return ret;
    }

    wait(request);

    debug_info("[LFI] End");
    if (request.error < 0) {
        return request.error;
    }
    return request.size;
}

int LFI::async_put(const void *buffer, size_t size, uint64_t remote_addr, uint64_t remote_key, lfi_request &request,
                   bool priority) {
    LFI_PROFILE_FUNCTION();
    auto comm_id = request.m_comm_id;

    auto [lock, comm] = get_comm_and_mutex(comm_id);
    std::unique_lock req_lock(request.mutex);

    if (!comm) return -LFI_COMM_NOT_FOUND;
    if (comm->is_canceled) return -LFI_BROKEN_COMM;

    request.reset();
    request.op_type = lfi_request::OpType::PUT;
    request.size = size;
    request.source = request.m_comm_id;

    if (!request.wait_context.load()) {
        request.wait_context.store(req_ctx_factory.create(request));
    }

    {
        std::unique_lock lock_pending(comm->m_endpoint.pending_ops_mutex);
        debug_info("[LFI] Save put to " << (priority ? "priority_ops " : "pending_ops ") << request);
        auto &queue = priority ? comm->m_endpoint.priority_ops : comm->m_endpoint.pending_ops;
        queue.push({lfi_pending_op::Type::PUT,
                    comm->m_endpoint.tx_endpoint(),
                    {buffer},
                    size,
                    comm->fi_addr,
                    remote_addr,
                    remote_key,
                    request.wait_context.load()});
    }

    debug_info("[LFI] async_put size " << size << " to " << format_lfi_comm{request.m_comm_id});
    req_lock.unlock();

    if (env::get_instance().LFI_fault_tolerance) {
        m_ft_manager.register_request(&request, 0, comm);
    }

    return LFI_SUCCESS;
}

int LFI::async_get(void *buffer, size_t size, uint64_t remote_addr, uint64_t remote_key, lfi_request &request,
                   bool priority) {
    LFI_PROFILE_FUNCTION();
    auto comm_id = request.m_comm_id;
    auto [lock, comm] = get_comm_and_mutex(comm_id);
    std::unique_lock req_lock(request.mutex);

    if (!comm) return -LFI_COMM_NOT_FOUND;
    if (comm->is_canceled) return -LFI_BROKEN_COMM;

    request.reset();
    request.op_type = lfi_request::OpType::GET;
    request.size = size;
    request.source = request.m_comm_id;

    if (!request.wait_context.load()) {
        request.wait_context.store(req_ctx_factory.create(request));
    }

    {
        std::unique_lock lock_pending(comm->m_endpoint.pending_ops_mutex);
        debug_info("[LFI] Save get to " << (priority ? "priority_ops " : "pending_ops ") << request);
        auto &queue = priority ? comm->m_endpoint.priority_ops : comm->m_endpoint.pending_ops;
        queue.push({lfi_pending_op::Type::GET,
                    comm->m_endpoint.tx_endpoint(),
                    {buffer},
                    size,
                    comm->fi_addr,
                    remote_addr,
                    remote_key,
                    request.wait_context.load()});
    }

    debug_info("[LFI] async_get size " << size << " from " << format_lfi_comm{request.m_comm_id});
    req_lock.unlock();

    if (env::get_instance().LFI_fault_tolerance) {
        m_ft_manager.register_request(&request, 0, comm);
    }

    return LFI_SUCCESS;
}

}  // namespace LFI
