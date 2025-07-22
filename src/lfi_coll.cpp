
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
#include <thread>

#include "impl/debug.hpp"
#include "impl/env.hpp"
#include "impl/lfi.hpp"
#include "impl/ns.hpp"
#include "impl/socket.hpp"
#include "lfi.h"
#include "lfi_async.h"

#ifdef __cplusplus
extern "C" {
#endif

double lfi_time(lfi_group *group) {
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
    debug_info("lfi_group_create(" << hostnames << ", " << n_hosts << ", " << out_group << ")");
    // reset group
    out_group->ranks = nullptr;
    out_group->size = 0;
    out_group->rank = -1;
    out_group->start_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::high_resolution_clock::now().time_since_epoch())
                                .count() *
                            0.000000001;

    if (n_hosts <= 0) {
        debug_info("lfi_group_create LFI_GROUP_INVAL");
        return -LFI_GROUP_INVAL;
    }
    auto hostname = LFI::ns::get_host_name();
    int hostname_index = -1;
    for (size_t i = 0; i < n_hosts; i++) {
        if (hostname == hostnames[i]) {
            hostname_index = i;
            break;
        }
    }

    debug_info("hostname_index " << hostname_index);
    if (hostname_index == -1) {
        debug_info("lfi_group_create LFI_GROUP_NO_SELF");
        return -LFI_GROUP_NO_SELF;
    }

    out_group->rank = hostname_index;
    out_group->size = n_hosts;
    out_group->ranks = (int *)::malloc(n_hosts * sizeof(int));

    for (size_t i = 0; i < n_hosts; i++) {
        out_group->ranks[i] = -2;
        debug_info("out_group->ranks[" << i << "] " << out_group->ranks[i]);
    }

    // int dummy = 0;
    int port = LFI::env::get_instance().LFI_group_port;
    // Is 2 seconds enough time for the timeout?
    int timeout_ms = 2000;
    for (size_t i = 0; i < n_hosts; i++) {
        debug_info("hostnames[" << i << "] " << hostnames[i]);
        if (hostname_index == static_cast<int>(i)) {
            out_group->ranks[i] = -1;
            for (size_t j = 1; j < n_hosts; j++) {
                if (out_group->ranks[j] == -2) {
                    int current_port = port + j;
                    debug_info("start server in " << current_port << " for " << hostnames[i]);
                    int server_fd = lfi_server_create(NULL, &current_port);
                    if (server_fd < 0) {
                        debug_error("lfi_server_create " << strerror(errno));
                        return server_fd;
                    }
                    out_group->ranks[j] = lfi_server_accept_t(server_fd, timeout_ms);
                    if (out_group->ranks[j] < 0) {
                        debug_error("lfi_server_accept_t " << strerror(errno));
                        return out_group->ranks[j];
                    }
                    // lfi_recv(out_group->ranks[j], &dummy, sizeof(dummy));
                    lfi_server_close(server_fd);
                }
            }

        } else {
            if (out_group->ranks[i] == -2) {
                int current_port = port + hostname_index;
                debug_info("start client in " << current_port << " for " << hostnames[i]);
                out_group->ranks[i] = lfi_client_create_t(hostnames[i], current_port, timeout_ms);
                if (out_group->ranks[i] < 0) {
                    return out_group->ranks[i];
                }
                // lfi_send(out_group->ranks[i], &dummy, sizeof(dummy));
            }
        }
    }

    debug_info("lfi_group_create Succcess self_index " << out_group->rank);
    lfi_barrier(out_group);

    return LFI_SUCCESS;
}

int lfi_group_rank(lfi_group *group, int *rank) {
    if (group->size == 0 || group->ranks == nullptr) {
        return -LFI_GROUP_NO_INIT;
    }
    (*rank) = group->rank;
    return LFI_SUCCESS;
}

int lfi_group_size(lfi_group *group, int *size) {
    if (group->size == 0 || group->ranks == nullptr) {
        return -LFI_GROUP_NO_INIT;
    }
    (*size) = group->size;
    return LFI_SUCCESS;
}

int lfi_group_close(lfi_group *group) {
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
                ret = lfi_trecv_async(recv_req, &dummy, 0, LFI_TAG_BARRIER);
                if (ret < 0) {
                    goto error;
                }
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
        ret = lfi_trecv_async(recv_req, &dummy, 0, LFI_TAG_BARRIER);
        if (ret < 0) {
            goto error;
        }
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