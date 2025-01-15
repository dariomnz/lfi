
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

#include <cstring>

#include "impl/debug.hpp"
#include "impl/lfi.hpp"
#include "impl/ns.hpp"
#include "impl/socket.hpp"

namespace LFI {

LFI::LFI() {
    int ret = 0;
    debug_info("[LFI] Start");
    if (!shm_ep.initialized()) {
        set_hints(shm_ep, "shm");
        ret = init(shm_ep);
        if (ret < 0) {
            set_hints(shm_ep, "sm2");
            ret = init(shm_ep);
            if (ret < 0) {
                throw std::runtime_error("LFI cannot init the intra-node endpoints");
            }
        }
        // Create LFI_ANY_COMM for shm_ep
        LFI::create_any_comm(shm_ep, ANY_COMM_SHM);
    }
    if (!peer_ep.initialized()) {
        set_hints(peer_ep, "");
        ret = init(peer_ep);
        if (ret < 0) {
            throw std::runtime_error("LFI cannot init the inter-node endpoints");
        }
        // Create LFI_ANY_COMM for peer_ep
        LFI::create_any_comm(peer_ep, ANY_COMM_PEER);
    }
    debug_info("[LFI] End = " << ret);
}

LFI::~LFI() {
    debug_info("[LFI] Start");
    ft_thread_destroy();

    if (shm_ep.initialized()) {
        destroy(shm_ep);
    }
    if (peer_ep.initialized()) {
        destroy(peer_ep);
    }
    debug_info("[LFI] End");
}

int LFI::set_hints(lfi_ep &lfi_ep, const std::string &prov) {
    debug_info("[LFI] Start");

    if (lfi_ep.hints != nullptr) {
        fi_freeinfo(lfi_ep.hints);
    }

    lfi_ep.hints = fi_allocinfo();
    if (!lfi_ep.hints) return -FI_ENOMEM;

    lfi_ep.hints->ep_attr->type = FI_EP_RDM;

    lfi_ep.hints->caps = FI_MSG | FI_TAGGED;

    lfi_ep.hints->tx_attr->op_flags = FI_DELIVERY_COMPLETE;

    lfi_ep.hints->mode = FI_CONTEXT;

    lfi_ep.hints->domain_attr->threading = FI_THREAD_SAFE;

    if (!prov.empty()) lfi_ep.hints->fabric_attr->prov_name = strdup(prov.c_str());

    debug_info("[LFI] End");

    return 0;
}

int LFI::init(lfi_ep &lfi_ep) {
    int ret;
    struct fi_cq_attr cq_attr = {};
    struct fi_av_attr av_attr = {};

    debug_info("[LFI] Start");

    ret = fi_getinfo(fi_version(), NULL, NULL, 0, lfi_ep.hints, &lfi_ep.info);

    debug_info("[LFI] fi_getinfo = " << ret);
    if (ret) {
        if (ret != -FI_ENODATA) {
            printf("fi_getinfo error (%d)\n", ret);
        }
        return ret;
    }

    debug_info("[LFI] " << fi_tostr(lfi_ep.info, FI_TYPE_INFO));
    debug_info("[LFI] provider: " << lfi_ep.info->lfi_attr->prov_name);

    ret = fi_fabric(lfi_ep.info->fabric_attr, &lfi_ep.fabric, NULL);
    debug_info("[LFI] fi_fabric = " << ret);
    if (ret) {
        printf("fi_fabric error (%d)\n", ret);
        return ret;
    }

    ret = fi_domain(lfi_ep.fabric, lfi_ep.info, &lfi_ep.domain, NULL);
    debug_info("[LFI] fi_domain = " << ret);
    if (ret) {
        printf("fi_domain error (%d)\n", ret);
        return ret;
    }

    cq_attr.format = FI_CQ_FORMAT_TAGGED;
    cq_attr.wait_obj = FI_WAIT_NONE;
    ret = fi_cq_open(lfi_ep.domain, &cq_attr, &lfi_ep.cq, NULL);
    debug_info("[LFI] fi_cq_open = " << ret);
    if (ret) {
        printf("fi_cq_open error (%d)\n", ret);
        return ret;
    }

    av_attr.type = FI_AV_MAP;
    ret = fi_av_open(lfi_ep.domain, &av_attr, &lfi_ep.av, NULL);
    debug_info("[LFI] fi_av_open = " << ret);
    if (ret) {
        printf("fi_av_open error (%d)\n", ret);
        return ret;
    }

    // Try opening a scalable endpoint if it is not posible a normal endpoint
    ret = fi_scalable_ep(lfi_ep.domain, lfi_ep.info, &lfi_ep.ep, NULL);
    debug_info("[LFI] fi_scalable_ep = " << ret);
    if (ret == -FI_ENOSYS) {
        lfi_ep.use_scalable_ep = false;
    } else if (ret) {
        printf("fi_scalable_ep error (%d)\n", ret);
        return ret;
    }

    if (lfi_ep.use_scalable_ep) {
        ret = fi_scalable_ep_bind(lfi_ep.ep, &lfi_ep.av->fid, 0);
        debug_info("[LFI] fi_scalable_ep_bind = " << ret);
        if (ret) {
            printf("fi_scalable_ep_bind av error (%d)\n", ret);
            return ret;
        }

        ret = fi_enable(lfi_ep.ep);
        debug_info("[LFI] fi_enable = " << ret);
        if (ret) {
            printf("fi_enable error (%d)\n", ret);
            return ret;
        }

        lfi_ep.info->tx_attr->caps |= FI_MSG;
        lfi_ep.info->tx_attr->caps |= FI_NAMED_RX_CTX; /* Required for scalable endpoints indexing */
        ret = fi_tx_context(lfi_ep.ep, 0, lfi_ep.info->tx_attr, &lfi_ep.tx_ep, NULL);
        debug_info("[LFI] fi_tx_context tx_ep = " << ret);
        if (ret) {
            printf("fi_tx_context error (%d)\n", ret);
            return ret;
        }

        ret = fi_ep_bind(lfi_ep.tx_ep, &lfi_ep.cq->fid, FI_SEND);
        debug_info("[LFI] fi_ep_bind tx_ep = " << ret);
        if (ret) {
            printf("fi_ep_bind error (%d)\n", ret);
            return ret;
        }

        ret = fi_enable(lfi_ep.tx_ep);
        debug_info("[LFI] fi_enable tx_ep = " << ret);
        if (ret) {
            printf("fi_enable error (%d)\n", ret);
            return ret;
        }

        lfi_ep.info->rx_attr->caps |= FI_MSG;
        lfi_ep.info->rx_attr->caps |= FI_NAMED_RX_CTX; /* Required for scalable endpoints indexing */
        ret = fi_rx_context(lfi_ep.ep, 0, lfi_ep.info->rx_attr, &lfi_ep.rx_ep, NULL);
        debug_info("[LFI] fi_rx_context rx_ep = " << ret);
        if (ret) {
            printf("fi_rx_context error (%d)\n", ret);
            return ret;
        }

        ret = fi_ep_bind(lfi_ep.rx_ep, &lfi_ep.cq->fid, FI_RECV);
        debug_info("[LFI] fi_ep_bind rx_ep = " << ret);
        if (ret) {
            printf("fi_ep_bind error (%d)\n", ret);
            return ret;
        }

        ret = fi_enable(lfi_ep.rx_ep);
        debug_info("[LFI] fi_enable rx_ep = " << ret);
        if (ret) {
            printf("fi_enable error (%d)\n", ret);
            return ret;
        }
    } else {
        ret = fi_endpoint(lfi_ep.domain, lfi_ep.info, &lfi_ep.ep, NULL);
        debug_info("[LFI] fi_endpoint = " << ret);
        if (ret) {
            printf("fi_endpoint error (%d)\n", ret);
            return ret;
        }

        ret = fi_ep_bind(lfi_ep.ep, &lfi_ep.av->fid, 0);
        debug_info("[LFI] fi_ep_bind = " << ret);
        if (ret) {
            printf("fi_ep_bind error (%d)\n", ret);
            return ret;
        }

        ret = fi_ep_bind(lfi_ep.ep, &lfi_ep.cq->fid, FI_SEND | FI_RECV);
        debug_info("[LFI] fi_ep_bind = " << ret);
        if (ret) {
            printf("fi_ep_bind error (%d)\n", ret);
            return ret;
        }

        ret = fi_enable(lfi_ep.ep);
        debug_info("[LFI] fi_enable = " << ret);
        if (ret) {
            printf("fi_enable error (%d)\n", ret);
            return ret;
        }
    }

    lfi_ep.enable_ep = true;

    ret = ft_thread_start();

    debug_info("[LFI] End = " << ret);

    return ret;
}

int LFI::destroy(lfi_ep &lfi_ep) {
    int ret = 0;

    debug_info("[LFI] Start");

    lfi_ep.enable_ep = false;

    if (lfi_ep.tx_ep) {
        debug_info("[LFI] Close tx_context");
        ret = fi_close(&lfi_ep.tx_ep->fid);
        if (ret) printf("warning: error closing tx_context (%d)\n", ret);
        lfi_ep.tx_ep = nullptr;
    }

    if (lfi_ep.rx_ep) {
        debug_info("[LFI] Close rx_context");
        ret = fi_close(&lfi_ep.rx_ep->fid);
        if (ret) printf("warning: error closing rx_context (%d)\n", ret);
        lfi_ep.rx_ep = nullptr;
    }

    if (lfi_ep.ep) {
        debug_info("[LFI] Close endpoint");
        ret = fi_close(&lfi_ep.ep->fid);
        if (ret) printf("warning: error closing EP (%d)\n", ret);
        lfi_ep.ep = nullptr;
    }

    if (lfi_ep.av) {
        debug_info("[LFI] Close address vector");
        ret = fi_close(&lfi_ep.av->fid);
        if (ret) printf("warning: error closing AV (%d)\n", ret);
        lfi_ep.av = nullptr;
    }

    if (lfi_ep.cq) {
        debug_info("[LFI] Close completion queue");
        ret = fi_close(&lfi_ep.cq->fid);
        if (ret) printf("warning: error closing CQ (%d)\n", ret);
        lfi_ep.cq = nullptr;
    }

    if (lfi_ep.domain) {
        debug_info("[LFI] Close domain");
        ret = fi_close(&lfi_ep.domain->fid);
        if (ret) printf("warning: error closing domain (%d)\n", ret);
        lfi_ep.domain = nullptr;
    }

    if (lfi_ep.fabric) {
        debug_info("[LFI] Close fabric");
        ret = fi_close(&lfi_ep.fabric->fid);
        if (ret) printf("warning: error closing fabric (%d)\n", ret);
        lfi_ep.fabric = nullptr;
    }

    if (lfi_ep.hints) {
        debug_info("[LFI] Free hints ");
        fi_freeinfo(lfi_ep.hints);
        lfi_ep.hints = nullptr;
    }

    if (lfi_ep.info) {
        debug_info("[LFI] Free info ");
        fi_freeinfo(lfi_ep.info);
        lfi_ep.info = nullptr;
    }

    debug_info("[LFI] End = " << ret);

    return ret;
}
}  // namespace LFI