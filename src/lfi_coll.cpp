
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
// #define DEBUG
#include "lfi_coll.h"

#include <chrono>

#include "impl/debug.hpp"
#include "impl/env.hpp"
#include "impl/lfi.hpp"
#include "impl/ns.hpp"
#include "impl/profiler.hpp"
#include "impl/socket.hpp"
#include "lfi.h"
#include "lfi_async.h"

std::ostream &operator<<(std::ostream &os, lfi_group group) {
    os << "lfi_group ";
    os << "rank " << group.rank;
    os << " size " << group.size;

    os << " ranks[";
    for (size_t i = 0; i < group.size; i++) {
        os << group.ranks[i];
        if (i != group.size - 1) {
            os << ", ";
        }
    }
    os << "]";
    return os;
}

#ifdef __cplusplus
extern "C" {
#endif

double lfi_time(lfi_group *group) {
    LFI_PROFILE_FUNCTION();
    if (group->size == 0 || group->ranks == nullptr) {
        return -LFI_GROUP_NO_INIT;
    }
    double actual = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::high_resolution_clock::now().time_since_epoch())
                        .count() *
                    0.000000001;
    return actual - group->start_time;
}

int lfi_group_create(const char *hostnames[], size_t n_hosts, lfi_group *out_group) {
    LFI_PROFILE_FUNCTION();
    debug_info("lfi_group_create(" << hostnames << ", " << n_hosts << ", " << out_group << ")");

    // 0. Initialize and Reset
    if (out_group) {
        out_group->ranks = nullptr;
        out_group->size = 0;
        out_group->rank = -1;
        out_group->start_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::high_resolution_clock::now().time_since_epoch())
                                    .count() *
                                0.000000001;
    }

    if (n_hosts <= 0 || hostnames == nullptr || out_group == nullptr) {
        return -LFI_GROUP_INVAL;
    }

    const int coord_port = LFI::env::get_instance().LFI_group_port;
    const int timeout_ms = 2000;
    auto my_hostname = LFI::ns::get_host_name();

    // 1. Setup local mesh server on an ephemeral port (port 0)
    int my_ephemeral_port = 0;
    int mesh_server_fd = lfi_server_create(NULL, &my_ephemeral_port);
    if (mesh_server_fd < 0) {
        debug_error("lfi_group_create: failed to create mesh server");
        return mesh_server_fd;
    }

    // 2. Coordination Phase
    int my_rank = -1;
    std::vector<int> port_table(n_hosts, -1);

    // Structure for exchange
    struct rank_info {
        char host[HOST_NAME_MAX];
        int port;
    };

    // Try to become Rank 0 (Master) if we match hostnames[0]
    int master_server_fd = -1;
    if (my_hostname == hostnames[0]) {
        int p = coord_port;
        master_server_fd = LFI::socket::server_init("", p);
    }

    if (master_server_fd >= 0) {
        // MASTER LOGIC
        my_rank = 0;
        port_table[0] = my_ephemeral_port;
        std::vector<int> worker_comms;
        worker_comms.reserve(n_hosts - 1);

        for (size_t i = 1; i < n_hosts; i++) {
            int comm = LFI::socket::accept(master_server_fd, timeout_ms * 10);
            if (comm < 0) {
                debug_error("lfi_group_create: Master failed to accept worker " << i);
                LFI::socket::close(master_server_fd);
                lfi_server_close(mesh_server_fd);
                return comm;
            }

            rank_info info;
            LFI::socket::recv(comm, &info, sizeof(info));

            // Assign rank based on hostname match and available slot
            int assigned = -1;
            for (size_t r = 1; r < n_hosts; r++) {
                if (port_table[r] == -1 && std::string(info.host) == hostnames[r]) {
                    assigned = r;
                    break;
                }
            }

            if (assigned == -1) {
                debug_error("lfi_group_create: No slot for host " << info.host);
                // Simple error handling for now
                assigned = -2;
            } else {
                port_table[assigned] = info.port;
            }

            LFI::socket::send(comm, &assigned, sizeof(assigned));
            worker_comms.push_back(comm);
        }

        // Send full port table to all workers
        for (int comm : worker_comms) {
            LFI::socket::send(comm, port_table.data(), n_hosts * sizeof(int));
            LFI::socket::close(comm);
        }
        LFI::socket::close(master_server_fd);

    } else {
        // WORKER LOGIC
        int master_comm = LFI::socket::client_init(hostnames[0], coord_port, timeout_ms * 10);
        if (master_comm < 0) {
            debug_error("lfi_group_create: Worker failed to connect to Master at " << hostnames[0]);
            lfi_server_close(mesh_server_fd);
            return master_comm;
        }

        rank_info my_info;
        std::strncpy(my_info.host, my_hostname.c_str(), HOST_NAME_MAX - 1);
        my_info.port = my_ephemeral_port;

        LFI::socket::send(master_comm, &my_info, sizeof(my_info));
        LFI::socket::recv(master_comm, &my_rank, sizeof(my_rank));

        if (my_rank < 0) {
            debug_error("lfi_group_create: Master refused registration");
            LFI::socket::close(master_comm);
            lfi_server_close(mesh_server_fd);
            return -LFI_GROUP_NO_SELF;
        }

        LFI::socket::recv(master_comm, port_table.data(), n_hosts * sizeof(int));
        LFI::socket::close(master_comm);
    }

    // 3. Initialize group structure
    out_group->size = n_hosts;
    out_group->rank = my_rank;
    out_group->ranks = (int *)::malloc(n_hosts * sizeof(int));
    if (!out_group->ranks) {
        lfi_server_close(mesh_server_fd);
        return -1;
    }
    for (size_t i = 0; i < n_hosts; i++) out_group->ranks[i] = -1;

    // 4. Establish full mesh using ephemeral ports
    for (size_t j = 0; j < n_hosts; j++) {
        if (static_cast<int>(j) == my_rank) continue;

        if (static_cast<int>(j) < my_rank) {
            // Connect to lower rank
            out_group->ranks[j] = lfi_client_create_t(hostnames[j], port_table[j], timeout_ms);
            if (out_group->ranks[j] < 0) {
                int err = out_group->ranks[j];
                lfi_server_close(mesh_server_fd);
                lfi_group_close(out_group);
                return err;
            }
        } else {
            // Accept from higher rank
            out_group->ranks[j] = lfi_server_accept_t(mesh_server_fd, timeout_ms);
            if (out_group->ranks[j] < 0) {
                int err = out_group->ranks[j];
                lfi_server_close(mesh_server_fd);
                lfi_group_close(out_group);
                return err;
            }
        }
    }

    lfi_server_close(mesh_server_fd);
    lfi_barrier(out_group);

    return LFI_SUCCESS;
}

int lfi_group_rank(lfi_group *group, int *rank) {
    LFI_PROFILE_FUNCTION();
    if (group->size == 0 || group->ranks == nullptr) {
        return -LFI_GROUP_NO_INIT;
    }
    (*rank) = group->rank;
    return LFI_SUCCESS;
}

int lfi_group_size(lfi_group *group, int *size) {
    LFI_PROFILE_FUNCTION();
    if (group->size == 0 || group->ranks == nullptr) {
        return -LFI_GROUP_NO_INIT;
    }
    (*size) = group->size;
    return LFI_SUCCESS;
}

int lfi_group_close(lfi_group *group) {
    LFI_PROFILE_FUNCTION();
    debug_info("lfi_group_close(" << group << ")");
    int ret = 0;
    int res = 0;
    for (size_t i = 0; i < group->size; i++) {
        if (group->ranks[i] < 0) continue;
        ret = lfi_client_close(group->ranks[i]);
        if (ret < 0) {
            res = ret;
        }
    }
    if (group->ranks != nullptr) {
        ::free(group->ranks);
        group->ranks = nullptr;
    }
    debug_info("lfi_group_close(" << group << ") = " << res);
    return res;
}

int lfi_barrier(lfi_group *group) {
    LFI_PROFILE_FUNCTION();
    debug_info("lfi_barrier(" << group << ")");
    if (group->size <= 0) {
        return -1;
    }
    ssize_t ret = 0;
    int dummy = 0;
    std::vector<lfi_request *> requests;
    int root = 0;
    if (group->rank == root) {
        requests.reserve(2 * (group->size - 1));
        for (size_t i = 0; i < group->size; i++) {
            if (static_cast<int>(i) != root) {
                auto recv_req = requests.emplace_back(lfi_request_create(group->ranks[i]));
                debug_info("lfi_trecv_async " << group->ranks[i] << " LFI_TAG_BARRIER");
                ret = lfi_trecv_async(recv_req, &dummy, 0, LFI_TAG_BARRIER);
                if (ret < 0) {
                    goto error;
                }
                debug_info("lfi_tsend_async " << group->ranks[i] << " LFI_TAG_BARRIER");
                auto send_req = requests.emplace_back(lfi_request_create(group->ranks[i]));
                ret = lfi_tsend_async(send_req, &dummy, 0, LFI_TAG_BARRIER);
                if (ret < 0) {
                    goto error;
                }
            }
        }
    } else {
        requests.reserve(2);
        auto recv_req = requests.emplace_back(lfi_request_create(group->ranks[root]));
        debug_info("lfi_trecv_async root " << group->ranks[root] << " LFI_TAG_BARRIER");
        ret = lfi_trecv_async(recv_req, &dummy, 0, LFI_TAG_BARRIER);
        if (ret < 0) {
            goto error;
        }
        debug_info("lfi_tsend_async root " << group->ranks[root] << " LFI_TAG_BARRIER");
        auto send_req = requests.emplace_back(lfi_request_create(group->ranks[root]));
        ret = lfi_tsend_async(send_req, &dummy, 0, LFI_TAG_BARRIER);
        if (ret < 0) {
            goto error;
        }
    }

    ret = 0;
    goto cleanup;
error:
    for (auto &&req : requests) {
        lfi_cancel(req);
    }
cleanup:
    lfi_wait_all(requests.data(), requests.size());
    for (auto &&req : requests) {
        lfi_request_free(req);
    }

    debug_info("lfi_barrier(" << group << ") = " << ret);
    return ret;
}

int lfi_broadcast(lfi_group *group, int root, void *data, size_t size) {
    LFI_PROFILE_FUNCTION();
    debug_info("lfi_broadcast(" << group << ", " << root << ", " << data << ", " << size << ")");
    if (group->size <= 0) {
        return -1;
    }
    ssize_t ret = 0;
    std::vector<lfi_request *> requests;
    if (group->rank == root) {
        requests.reserve(group->size - 1);
        for (size_t i = 0; i < group->size; i++) {
            if (static_cast<int>(i) != root) {
                auto send_req = requests.emplace_back(lfi_request_create(group->ranks[i]));
                debug_info("send to " << group->ranks[i] << " size " << size);
                ret = lfi_tsend_async(send_req, data, size, LFI_TAG_BROADCAST);
                if (ret < 0) {
                    goto error;
                }
            }
        }
    } else {
        requests.reserve(1);
        auto recv_req = requests.emplace_back(lfi_request_create(group->ranks[root]));
        debug_info("recv from " << group->ranks[root] << " size " << size);
        ret = lfi_trecv_async(recv_req, data, size, LFI_TAG_BROADCAST);
        if (ret < 0) {
            goto error;
        }
    }
    ret = 0;
    goto cleanup;
error:
    for (auto &&req : requests) {
        lfi_cancel(req);
    }
cleanup:
    lfi_wait_all(requests.data(), requests.size());
    for (auto &&req : requests) {
        lfi_request_free(req);
    }
    debug_info("lfi_broadcast(" << group << ", " << root << ", " << data << ", " << size << ") = " << ret);
    return ret;
}

#ifdef __cplusplus
}
#endif

static std::unordered_map<lfi_op_type_enum, size_t> type_size = {
    {lfi_op_type_enum::LFI_OP_TYPE_INT, sizeof(int)},
    {lfi_op_type_enum::LFI_OP_TYPE_UINT32_T, sizeof(uint32_t)},
    {lfi_op_type_enum::LFI_OP_TYPE_INT32_T, sizeof(int32_t)},
    {lfi_op_type_enum::LFI_OP_TYPE_UINT64_T, sizeof(uint64_t)},
    {lfi_op_type_enum::LFI_OP_TYPE_INT64_T, sizeof(int64_t)},
};

template <typename T>
static T operate(enum lfi_op_enum op, void *data, std::vector<uint64_t> buffers) {
    T result;
    std::memcpy(&result, data, sizeof(T));
    for (auto &&buf : buffers) {
        T value;
        std::memcpy(&value, &buf, sizeof(T));
        switch (op) {
            case lfi_op_enum::LFI_OP_MIN: {
                if (value < result) result = value;
            } break;
            case lfi_op_enum::LFI_OP_MAX: {
                if (value > result) result = value;
            } break;
            case lfi_op_enum::LFI_OP_SUM: {
                result = result + value;
            } break;
            case lfi_op_enum::LFI_OP_PROD: {
                result = result * value;
            } break;
        }
    }
    return result;
}

#ifdef __cplusplus
extern "C" {
#endif
int lfi_allreduce(lfi_group *group, void *data, enum lfi_op_type_enum type, enum lfi_op_enum op) {
    LFI_PROFILE_FUNCTION();
    debug_info("lfi_allreduce(" << group << ")");
    if (group->size <= 0) {
        return -1;
    }

    ssize_t ret = 0;
    std::vector<lfi_request *> requests;
    // Only support types with up to 64 bits for buffer reduction
    std::vector<uint64_t> gather_buffers;
    int root = 0;

    if (group->rank == root) {
        requests.reserve(group->size - 1);
        gather_buffers.resize(group->size - 1, 0);
        int buf_idx = 0;
        for (size_t i = 0; i < group->size; i++) {
            if (static_cast<int>(i) != root) {
                auto recv_req = requests.emplace_back(lfi_request_create(group->ranks[i]));
                ret = lfi_trecv_async(recv_req, &gather_buffers[buf_idx++], type_size[type], LFI_TAG_ALLREDUCE);
                if (ret < 0) {
                    goto error;
                }
            }
        }
    } else {
        requests.reserve(1);
        auto send_req = requests.emplace_back(lfi_request_create(group->ranks[root]));
        ret = lfi_tsend_async(send_req, data, type_size[type], LFI_TAG_ALLREDUCE);
        if (ret < 0) {
            goto error;
        }
    }

    lfi_wait_all(requests.data(), requests.size());
    for (auto &&req : requests) {
        lfi_request_free(req);
    }
    requests.clear();

    if (group->rank == root) {
        switch (type) {
            case lfi_op_type_enum::LFI_OP_TYPE_INT: {
                *static_cast<int *>(data) = operate<int>(op, data, gather_buffers);
            } break;
            case lfi_op_type_enum::LFI_OP_TYPE_INT32_T: {
                *static_cast<int32_t *>(data) = operate<int32_t>(op, data, gather_buffers);
            } break;
            case lfi_op_type_enum::LFI_OP_TYPE_UINT32_T: {
                *static_cast<uint32_t *>(data) = operate<uint32_t>(op, data, gather_buffers);
            } break;
            case lfi_op_type_enum::LFI_OP_TYPE_INT64_T: {
                *static_cast<int64_t *>(data) = operate<int64_t>(op, data, gather_buffers);
            } break;
            case lfi_op_type_enum::LFI_OP_TYPE_UINT64_T: {
                *static_cast<uint64_t *>(data) = operate<uint64_t>(op, data, gather_buffers);
            } break;
            default: {
                ret = -1;
                goto error;
            } break;
        }

        requests.reserve(group->size - 1);
        for (size_t i = 0; i < group->size; i++) {
            if (static_cast<int>(i) != root) {
                auto bcast_req = requests.emplace_back(lfi_request_create(group->ranks[i]));
                ret = lfi_tsend_async(bcast_req, data, type_size[type], LFI_TAG_ALLREDUCE);
                if (ret < 0) {
                    goto error;
                }
            }
        }
    } else {
        requests.reserve(1);
        auto bcast_req = requests.emplace_back(lfi_request_create(group->ranks[root]));
        ret = lfi_trecv_async(bcast_req, data, type_size[type], LFI_TAG_ALLREDUCE);
        if (ret < 0) {
            goto error;
        }
    }

    ret = 0;
    goto cleanup;
error:
    for (auto &&req : requests) {
        lfi_cancel(req);
    }
cleanup:
    lfi_wait_all(requests.data(), requests.size());
    for (auto &&req : requests) {
        lfi_request_free(req);
    }

    debug_info("lfi_allreduce(" << group << ") = " << ret);
    return ret;
}

#ifdef __cplusplus
}
#endif