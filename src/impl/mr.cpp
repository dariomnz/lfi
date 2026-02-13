
#include <rdma/fi_domain.h>

#include "impl/debug.hpp"
#include "impl/lfi.hpp"
#include "lfi_error.h"

namespace LFI {

int LFI::mr_reg(void *addr, size_t size) {
    debug_info("(" << addr << ", " << size << ") >> Begin");

    auto mr = std::make_unique<lfi_mr>();
    mr->addr = addr;
    mr->size = size;

    int ret;
    // Register with peer domain
    if (peer_ep.initialized()) {
        ret = fi_mr_reg(peer_ep.domain, addr, size, FI_WRITE | FI_READ | FI_REMOTE_WRITE | FI_REMOTE_READ, 0, 0, 0,
                        &mr->peer_mr, nullptr);
        debug_info("fi_mr_reg peer " << ret);
        if (ret < 0) {
            debug_error("fi_mr_reg peer error: " << fi_strerror(ret));
            return -LFI_LIBFABRIC_ERROR;
        }
        ret = fi_mr_key(mr->peer_mr);
        if (ret < 0) {
            debug_error("fi_mr_key peer error: " << fi_strerror(ret));
            return -LFI_LIBFABRIC_ERROR;
        }
        mr->peer_key = ret;
        debug_info("fi_mr_reg peer key " << mr->peer_key);
    }

    // Register with shm domain
    if (shm_ep.initialized() && shm_ep.domain != peer_ep.domain) {
        ret = fi_mr_reg(shm_ep.domain, addr, size, FI_WRITE | FI_READ | FI_REMOTE_WRITE | FI_REMOTE_READ, 0, 0, 0,
                        &mr->shm_mr, nullptr);
        debug_info("fi_mr_reg shm " << ret);
        if (ret < 0) {
            debug_error("fi_mr_reg shm error: " << fi_strerror(ret));
            if (mr->peer_mr) fi_close(&mr->peer_mr->fid);
            return -LFI_LIBFABRIC_ERROR;
        }
        ret = fi_mr_key(mr->shm_mr);
        if (ret < 0) {
            debug_error("fi_mr_key shm error: " << fi_strerror(ret));
            if (mr->peer_mr) fi_close(&mr->peer_mr->fid);
            return -LFI_LIBFABRIC_ERROR;
        }
        mr->shm_key = ret;
        debug_info("fi_mr_reg shm key " << mr->shm_key);
    } else {
        mr->shm_key = mr->peer_key;
    }

    if (mr->peer_key != mr->shm_key) {
        debug_info("Peer key " << mr->peer_key << " != shm key " << mr->shm_key);
        return -LFI_LIBFABRIC_ERROR;
    }

    int key = mr->peer_key;
    m_mrs.emplace(key, std::move(mr));

    debug_info("(" << addr << ", " << size << ")=" << key << " >> End");
    return key;
}

int LFI::mr_unreg(int key) {
    debug_info("(" << key << ") >> Begin");
    std::unique_lock lock(m_mr_mutex);
    auto it = m_mrs.find(key);
    if (it == m_mrs.end()) {
        debug_error("Memory region not found: " << key);
        return -LFI_MR_NOT_FOUND;
    }

    auto &mr = it->second;
    if (mr->peer_mr) fi_close(&mr->peer_mr->fid);
    if (mr->shm_mr) fi_close(&mr->shm_mr->fid);

    m_mrs.erase(it);
    debug_info("(" << key << ") >> End");
    return LFI_SUCCESS;
}
}  // namespace LFI
