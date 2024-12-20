
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

#include "impl/fabric.hpp"
#include "impl/debug.hpp"
#include "impl/env.hpp"

#include "sstream"

namespace LFI
{
    static inline std::string fi_flags_to_string(uint64_t flags)
    {
        std::stringstream out;
        if (flags & FI_MSG) { out << "    FI_MSG" << std::endl; }
        if (flags & FI_RMA) { out << "    FI_RMA" << std::endl; }
        if (flags & FI_TAGGED) { out << "    FI_TAGGED" << std::endl; }
        if (flags & FI_ATOMIC) { out << "    FI_ATOMIC" << std::endl; }
        if (flags & FI_MULTICAST) { out << "    FI_MULTICAST" << std::endl; }
        if (flags & FI_COLLECTIVE) { out << "    FI_COLLECTIVE" << std::endl; }

        if (flags & FI_READ) { out << "    FI_READ" << std::endl; }
        if (flags & FI_WRITE) { out << "    FI_WRITE" << std::endl; }
        if (flags & FI_RECV) { out << "    FI_RECV" << std::endl; }
        if (flags & FI_SEND) { out << "    FI_SEND" << std::endl; }
        if (flags & FI_REMOTE_READ) { out << "    FI_REMOTE_READ" << std::endl; }
        if (flags & FI_REMOTE_WRITE) { out << "    FI_REMOTE_WRITE" << std::endl; }

        if (flags & FI_MULTI_RECV) { out << "    FI_MULTI_RECV" << std::endl; }
        if (flags & FI_REMOTE_CQ_DATA) { out << "    FI_REMOTE_CQ_DATA" << std::endl; }
        if (flags & FI_MORE) { out << "    FI_MORE" << std::endl; }
        if (flags & FI_PEEK) { out << "    FI_PEEK" << std::endl; }
        if (flags & FI_TRIGGER) { out << "    FI_TRIGGER" << std::endl; }
        if (flags & FI_FENCE) { out << "    FI_FENCE" << std::endl; }
        // if (flags & FI_PRIORITY) { out << "    FI_PRIORITY" << std::endl; }

        if (flags & FI_COMPLETION) { out << "    FI_COMPLETION" << std::endl; }
        if (flags & FI_INJECT) { out << "    FI_INJECT" << std::endl; }
        if (flags & FI_INJECT_COMPLETE) { out << "    FI_INJECT_COMPLETE" << std::endl; }
        if (flags & FI_TRANSMIT_COMPLETE) { out << "    FI_TRANSMIT_COMPLETE" << std::endl; }
        if (flags & FI_DELIVERY_COMPLETE) { out << "    FI_DELIVERY_COMPLETE" << std::endl; }
        if (flags & FI_AFFINITY) { out << "    FI_AFFINITY" << std::endl; }
        if (flags & FI_COMMIT_COMPLETE) { out << "    FI_COMMIT_COMPLETE" << std::endl; }
        if (flags & FI_MATCH_COMPLETE) { out << "    FI_MATCH_COMPLETE" << std::endl; }

        if (flags & FI_HMEM) { out << "    FI_HMEM" << std::endl; }
        if (flags & FI_VARIABLE_MSG) { out << "    FI_VARIABLE_MSG" << std::endl; }
        if (flags & FI_RMA_PMEM) { out << "    FI_RMA_PMEM" << std::endl; }
        if (flags & FI_SOURCE_ERR) { out << "    FI_SOURCE_ERR" << std::endl; }
        if (flags & FI_LOCAL_COMM) { out << "    FI_LOCAL_COMM" << std::endl; }
        if (flags & FI_REMOTE_COMM) { out << "    FI_REMOTE_COMM" << std::endl; }
        if (flags & FI_SHARED_AV) { out << "    FI_SHARED_AV" << std::endl; }
        if (flags & FI_PROV_ATTR_ONLY) { out << "    FI_PROV_ATTR_ONLY" << std::endl; }
        if (flags & FI_NUMERICHOST) { out << "    FI_NUMERICHOST" << std::endl; }
        if (flags & FI_RMA_EVENT) { out << "    FI_RMA_EVENT" << std::endl; }
        if (flags & FI_SOURCE) { out << "    FI_SOURCE" << std::endl; }
        if (flags & FI_NAMED_RX_CTX) { out << "    FI_NAMED_RX_CTX" << std::endl; }
        if (flags & FI_DIRECTED_RECV) { out << "    FI_DIRECTED_RECV" << std::endl; }
        return out.str();
    }

    static inline std::string fi_cq_tagged_entry_to_string(const fi_cq_tagged_entry &entry)
    {
        std::stringstream out;
        out << "fi_cq_tagged_entry:" << std::endl;
        out << "  op_context: " << entry.op_context << std::endl;
        out << "  Flags set:" << std::endl;
        out << fi_flags_to_string(entry.flags);
        out << "  len: " << entry.len << std::endl;
        out << "  buf: " << entry.buf << std::endl;
        out << "  data: " << entry.data << std::endl;
        out << "  tag: " << entry.tag << std::endl;
        if (entry.flags & FI_RECV){
            out << "    real_tag: " << (entry.tag & 0x0000'0000'0000'FFFF) << std::endl;
            out << "    rank_peer: " << ((entry.tag & 0x0000'00FF'FFFF'0000) >> 16) << std::endl;
            out << "    rank_self_in_peer: " << ((entry.tag & 0xFFFF'FF00'0000'0000) >> 40) << std::endl;
        }
        return out.str();
    }

    static inline std::string fi_cq_err_entry_to_string(const fi_cq_err_entry &entry, fid_cq *cq)
    {
        std::stringstream out;
        out << "fi_cq_err_entry:" << std::endl;
        out << "  op_context: " << entry.op_context << std::endl;
        out << "  Flags set:" << std::endl;
        out << fi_flags_to_string(entry.flags);
        out << "  len: " << entry.len << std::endl;
        out << "  buf: " << entry.buf << std::endl;
        out << "  data: " << entry.data << std::endl;
        out << "  tag: " << entry.tag << std::endl;
        out << "    real_tag: " << (entry.tag & 0x0000'0000'0000'FFFF) << std::endl;
        out << "    rank_peer: " << ((entry.tag & 0x0000'00FF'FFFF'0000) >> 16) << std::endl;
        out << "    rank_self_in_peer: " << ((entry.tag & 0xFFFF'FF00'0000'0000) >> 40) << std::endl;
        out << "  olen: " << entry.olen << std::endl;
        out << "  err: " << entry.err << " " << fi_strerror(entry.err) << std::endl;
        out << "  prov_errno: " << entry.prov_errno << " " << fi_cq_strerror(cq, entry.prov_errno, entry.err_data, NULL, 0) << std::endl;
        out << "  err_data: " << entry.err_data << std::endl;
        out << "  err_data_size: " << entry.err_data_size << std::endl;
        return out.str();
    }

    fabric_request fabric_request::Create(uint32_t comm_id) {
        LFI& lfi = LFI::get_instance();
        fabric_comm *comm = lfi.get_comm(comm_id);
        if (comm == nullptr){
            throw std::runtime_error("fabric_comm not found");
        }
        return fabric_request{*comm};
    }

    int LFI::progress(fabric_request &request)
    {
        int ret;
        const int comp_count = 8;
        struct fi_cq_tagged_entry comp[comp_count] = {};
        // sizeof(comp)
        
        // Libfabric progress
        ret = fi_cq_read(request.m_comm.m_ep.cq, comp, comp_count);
        if (ret == -FI_EAGAIN)
        {
            return 0;
        }

        // TODO: handle error
        if (ret == -FI_EAVAIL)
        {
            debug_info("[Error] fi_cq_read "<<ret<<" "<<fi_strerror(ret));
            fi_cq_err_entry err;
            fi_cq_readerr(request.m_comm.m_ep.cq, &err, 0);
            debug_info(fi_cq_err_entry_to_string(err, request.m_comm.m_ep.cq));

            if (std::abs(err.err) == FI_ECANCELED && err.op_context != (void*)(&request)) return ret;
            
            // Only report when the ptr to the request is the same as the actual request when canceled
            // because it can be free because the canceled is not always waited 
            fabric_request *request_p = static_cast<fabric_request *>(err.op_context);
            std::unique_lock lock(request_p->mutex);
            debug_info("[Error] "<<request_p->to_string()<<" cq error");
            request_p->wait_context = false;
            if (std::abs(err.err) == FI_ECANCELED){
                request_p->error = -LFI_CANCELED;
            }else if (std::abs(err.err) == FI_ENOMSG){
                request_p->error = -LFI_PEEK_NO_MSG;
            }else{
                request_p->error = -LFI_ERROR;
            }
            request_p->cv.notify_all();
            return ret;
        }

        // Handle the cq entries
        for (int i = 0; i < ret; i++)
        {
            fabric_request *request = static_cast<fabric_request *>(comp[i].op_context);
            debug_info(fi_cq_tagged_entry_to_string(comp[i]));

            std::unique_lock lock(request->mutex);
            request->entry = comp[i];
            request->wait_context = false;
            request->cv.notify_all();
        }
        // print("lfi process "<<ret<<" num entrys");
        return ret;
    }

    bool LFI::wait_check_timeout(int32_t timeout_ms, decltype(std::chrono::high_resolution_clock::now()) start)
    {
        int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count();
        // debug_info("[LFI] Check timeout of "<<timeout_ms<<" ms with elapsed "<<elapsed_ms<<" ms")
        if (elapsed_ms >= timeout_ms){
            // int ret = cancel(request);
            // if (ret < 0){
            //     print("TODO: check error in fi_cancel");
            // }
            return true;
        }
        return false;
    }

    int LFI::wait(fabric_request &request, int32_t timeout_ms)
    {
        debug_info("[LFI] Start "<<request.to_string()<<" timeout_ms "<<timeout_ms);

        // Check cancelled comm
        if (request.m_comm.is_canceled){
            return -LFI_CANCELED;
        }

        std::unique_lock global_lock(request.m_comm.m_ep.mutex_ep, std::defer_lock);
        std::unique_lock request_lock(request.mutex);
        decltype(std::chrono::high_resolution_clock::now()) start;
        bool is_timeout = false;
        if (timeout_ms >= 0){
            start = std::chrono::high_resolution_clock::now();
        }

        while (request.wait_context && !is_timeout)
        {
            if (global_lock.try_lock()){
                while (request.wait_context && !is_timeout)
                {
                    request_lock.unlock();
                    progress(request);
                    if (timeout_ms >= 0){
                        is_timeout = wait_check_timeout(timeout_ms, start);
                    }
                    request_lock.lock();
                }
                global_lock.unlock();
            }else{
                if (request.wait_context){
                    request.cv.wait_for(request_lock, std::chrono::milliseconds(10));
                }
            }
            if(timeout_ms >= 0){
                if (is_timeout) continue;
                is_timeout = wait_check_timeout(timeout_ms, start);
            }
        }
        request_lock.unlock();
        // Return timeout only if is not completed and timeout
        if (is_timeout && request.wait_context){
            request.error = -LFI_TIMEOUT;
            debug_info("[LFI] End wait with timeout "<<request.to_string());
            return -LFI_TIMEOUT;
        }

        if (env::get_instance().LFI_fault_tolerance){
            std::unique_lock lock(request.m_comm.ft_mutex);
            debug_info("[LFI] erase request "<<request.to_string()<<" in comm "<<request.m_comm.rank_peer);
            request.m_comm.ft_requests.erase(&request);
        }

        debug_info("[LFI] End wait "<<request.to_string());
        return LFI_SUCCESS;
    }

    int LFI::cancel(fabric_request &request){
        // The inject is not cancelled
        debug_info("[LFI] Start "<<request.to_string());
        if (request.is_inject) return 0;

        fid_ep *p_ep = nullptr;
        if (request.is_send){
            p_ep = request.m_comm.m_ep.use_scalable_ep ? request.m_comm.m_ep.tx_ep : request.m_comm.m_ep.ep;
        }else{
            p_ep = request.m_comm.m_ep.use_scalable_ep ? request.m_comm.m_ep.rx_ep : request.m_comm.m_ep.ep;
        }
        // Cancel request and notify 
        int ret = 0;
        
        // Ignore return value 
        // ref: https://github.com/ofiwg/libfabric/issues/7795
        fi_cancel(&p_ep->fid, &request);
        debug_info("fi_cancel ret "<<ret<<" "<<fi_strerror(ret));

        if (request.is_send == true){
            // Try one progress to read the canceled and not accumulate errors
            std::unique_lock global_lock(request.m_comm.m_ep.mutex_ep, std::defer_lock);
            if (global_lock.try_lock()){
                progress(request);
                global_lock.unlock();
            }
            
            // Check if completed to no report error
            if (request.wait_context){
                std::unique_lock lock(request.mutex);
                request.wait_context = false;
                request.error = -LFI_CANCELED;
                request.cv.notify_all();
            }
        }else{
            // For the recvs wait to the completion of the cancel
            ret = wait(request);
        }
        
        debug_info("[LFI] End "<<std::hex<<&request<<std::dec);
        return ret;
    }

    fabric_msg LFI::send(uint32_t comm_id, const void *buffer, size_t size, uint32_t tag)
    {
        fabric_msg msg = {};
        debug_info("[LFI] Start");

        // Check if any_comm in send is error
        if (comm_id == LFI_ANY_COMM_SHM || comm_id == LFI_ANY_COMM_PEER){
            msg.error = -LFI_ERROR;
            return msg;
        }

        // Check if comm exists
        fabric_comm *comm = get_comm(comm_id);
        if (comm == nullptr){
            msg.error = -LFI_COMM_NOT_FOUND;
            return msg;
        }
        fabric_request request(*comm);

        msg = async_send(buffer, size, tag, request);

        if (msg.error < 0){
            return msg;
        }

        wait(request);

        msg.error = request.error;

        debug_info("[LFI] End");
        return msg;
    }

    fabric_msg LFI::recv(uint32_t comm_id, void *buffer, size_t size, uint32_t tag)
    {
        fabric_msg msg = {};
        debug_info("[LFI] Start");

        // Check if any_comm, is necesary to be any
        if (comm_id == LFI_ANY_COMM_SHM || comm_id == LFI_ANY_COMM_PEER){
            return any_recv(buffer, size, tag);
        }

        // Check if comm exists
        fabric_comm *comm = get_comm(comm_id);
        if (comm == nullptr){
            msg.error = -LFI_COMM_NOT_FOUND;
            return msg;
        }
        fabric_request request(*comm);

        msg = async_recv(buffer, size, tag, request);
        
        if (msg.error < 0){
            return msg;
        }

        wait(request);

        msg.error = request.error;
        msg.size = request.entry.len;

        debug_info("[LFI] End");
        return msg;
    }

#ifndef LFI_ANY_RECV_WITHOUT_PEEK 
    fabric_msg LFI::any_recv(void *buffer, size_t size, uint32_t tag)
    {
        fabric_msg peer_msg = {}, shm_msg = {};
        debug_info("[LFI] Start");

        fabric_msg* recv_msg = nullptr;
        bool finish = false;
        while(!finish){
            // Try a recv in shm
            peer_msg = recv_peek(LFI_ANY_COMM_SHM, buffer, size, tag);
            if (peer_msg.error != -LFI_PEEK_NO_MSG){
                recv_msg = &peer_msg;
                break;
            }
            shm_msg = recv_peek(LFI_ANY_COMM_PEER, buffer, size, tag);
            if (peer_msg.error != -LFI_PEEK_NO_MSG){
                recv_msg = &peer_msg;
                break;
            }
        }
        
        debug_info("[LFI] End");
        return *recv_msg;
    }
#else
    fabric_msg LFI::any_recv(void *buffer, size_t size, uint32_t tag)
    {
        fabric_msg peer_msg = {}, shm_msg = {};
        debug_info("[LFI] Start");

        // For the shm
        fabric_comm *comm = get_comm(LFI_ANY_COMM_SHM);
        if (comm == nullptr){
            throw std::runtime_error("There are no LFI_ANY_COMM_SHM. This should not happend");
        }
        fabric_request shm_request(*comm);
        // For the peer
        comm = get_comm(LFI_ANY_COMM_PEER);
        if (comm == nullptr){
            throw std::runtime_error("There are no LFI_ANY_COMM_PEER. This should not happend");
        }
        fabric_request peer_request(*comm);
        
        fabric_request* request = nullptr;
        bool finish = false;
        int ret = 0;
        while(!finish){
            // Try a recv in shm
            shm_msg = async_recv(buffer, size, tag, shm_request);
            if (shm_msg.error < 0){
                return shm_msg;
            }
            ret = wait(shm_request, 0);
            if (ret != -LFI_TIMEOUT){
                request = &shm_request;
                break;
            }
            // it can be succesfully completed in the cancel
            ret = cancel(shm_request);
            if (ret < 0 || shm_request.error == 0){
                request = &shm_request;
                break;
            }

            // Try a recv in peer
            peer_msg = async_recv(buffer, size, tag, peer_request);
            if (peer_msg.error < 0){
                return peer_msg;
            }
            ret = wait(peer_request, 0);
            if (ret != -LFI_TIMEOUT){
                request = &peer_request;
                break;
            }
            // it can be succesfully completed in the cancel
            ret = cancel(peer_request);
            if (ret < 0 || peer_request.error == 0){
                request = &peer_request;
                break;
            }
        }
        
        fabric_msg msg;
        msg.error = request->error;
        msg.size = request->entry.len;
        msg.tag = request->entry.tag & 0x0000'0000'0000'FFFF;
        msg.rank_self_in_peer = (request->entry.tag & 0xFFFF'FF00'0000'0000) >> 40;
        msg.rank_peer = (request->entry.tag & 0x0000'00FF'FFFF'0000) >> 16;
        debug_info("[LFI] End");
        return msg;
    }
#endif

    fabric_msg LFI::async_send(const void *buffer, size_t size, uint32_t tag, fabric_request& request, int32_t timeout_ms)
    {
        int ret;
        fabric_msg msg = {};

        // Check cancelled comm
        if (request.m_comm.is_canceled){
            msg.error = -LFI_CANCELED_COMM;
            return msg;
        }

        // Check if any_comm in send is error
        if (request.m_comm.rank_peer == LFI_ANY_COMM_SHM || request.m_comm.rank_peer == LFI_ANY_COMM_PEER)
        {
            msg.error = -LFI_ERROR;
            return msg;
        }

        request.reset();

        decltype(std::chrono::high_resolution_clock::now()) start;
        if (timeout_ms >= 0){
            start = std::chrono::high_resolution_clock::now();
        }
        // tag format 24 bits rank_peer 24 bits rank_self_in_peer 16 bits tag
        uint64_t aux_rank_peer = request.m_comm.rank_peer;
        uint64_t aux_rank_self_in_peer = request.m_comm.rank_self_in_peer;
        uint64_t aux_tag = tag;
        uint64_t tag_send = (aux_rank_peer << 40) | (aux_rank_self_in_peer << 16) | aux_tag;

        debug_info("[LFI] Start size " << size << " rank_peer " << request.m_comm.rank_peer << " rank_self_in_peer " << request.m_comm.rank_self_in_peer << " tag " << tag << " send_context " << (void *)&request.context);

        request.is_send = true;
        if (size > request.m_comm.m_ep.info->tx_attr->inject_size)
        {
            fid_ep *p_tx_ep = request.m_comm.m_ep.use_scalable_ep ? request.m_comm.m_ep.tx_ep : request.m_comm.m_ep.ep;
            request.wait_context = true;
            do
            {
                {
                    std::unique_lock global_lock(request.m_comm.m_ep.mutex_send_recv);
                    ret = fi_tsend(p_tx_ep, buffer, size, NULL, request.m_comm.fi_addr, tag_send, &request.context);
                }

                if (ret == -FI_EAGAIN){
                    std::unique_lock global_lock(request.m_comm.m_ep.mutex_ep, std::defer_lock);
                    if (global_lock.try_lock()){
                        progress(request);
                        global_lock.unlock();
                    }
                    
                    if (timeout_ms >= 0){
                        int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count(); 
                        if (elapsed_ms >= timeout_ms){
                            msg.error = -LFI_TIMEOUT;
                            return msg;
                        }
                    }

                    if (request.m_comm.is_canceled){
                        msg.error = -LFI_CANCELED_COMM;
                        return msg;
                    }
                }
            } while (ret == -FI_EAGAIN);

            if (env::get_instance().LFI_fault_tolerance && ret == 0){
                std::unique_lock lock(request.m_comm.ft_mutex);
                debug_info("[LFI] insert request "<<std::hex<<&request<<std::dec<<" in comm "<<request.m_comm.rank_peer);
                request.m_comm.ft_requests.insert(&request);
            }

            debug_info("[LFI] Waiting on rank_peer " << request.m_comm.rank_peer);
        }
        else
        {
            fid_ep *p_tx_ep = request.m_comm.m_ep.use_scalable_ep ? request.m_comm.m_ep.tx_ep : request.m_comm.m_ep.ep;
            request.wait_context = false;
            request.is_inject = true;
            do
            {
                {
                    std::unique_lock global_lock(request.m_comm.m_ep.mutex_send_recv);
                    ret = fi_tinject(p_tx_ep, buffer, size, request.m_comm.fi_addr, tag_send);
                }

                if (ret == -FI_EAGAIN){
                    std::unique_lock global_lock(request.m_comm.m_ep.mutex_ep, std::defer_lock);
                    if (global_lock.try_lock()){
                        progress(request);
                        global_lock.unlock();
                    }
                    
                    if (timeout_ms >= 0){
                        int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count(); 
                        if (elapsed_ms >= timeout_ms){
                            msg.error = -LFI_TIMEOUT;
                            return msg;
                        }
                    }

                    if (request.m_comm.is_canceled){
                        msg.error = -LFI_CANCELED_COMM;
                        return msg;
                    }
                }
            } while (ret == -FI_EAGAIN);

            // To not wait in this request
            debug_info("[LFI] fi_tinject of " << size << " for rank_peer " << request.m_comm.rank_peer);
        }

        if (ret != 0)
        {
            printf("error posting send buffer (%d)\n", ret);
            msg.error = -LFI_ERROR;
            return msg;
        }

        msg.size = size;
        msg.tag = tag_send & 0x0000'0000'0000'FFFF;
        msg.rank_peer = (tag_send & 0xFFFF'FF00'0000'0000) >> 40;
        msg.rank_self_in_peer = (tag_send & 0x0000'00FF'FFFF'0000) >> 16;

        debug_info("[LFI] msg size " << msg.size << " rank_peer " << msg.rank_peer << " rank_self_in_peer " << msg.rank_self_in_peer << " tag " << msg.tag << " error " << msg.error);
        debug_info("[LFI] End = " << size);
        return msg;
    }

    fabric_msg LFI::async_recv(void *buffer, size_t size, uint32_t tag, fabric_request& request, int32_t timeout_ms)
    {
        int ret;
        fabric_msg msg = {};

        // Check cancelled comm
        if (request.m_comm.is_canceled){
            msg.error = -LFI_CANCELED_COMM;
            return msg;
        }

        request.reset();
        uint64_t mask = 0;
        decltype(std::chrono::high_resolution_clock::now()) start;
        if (timeout_ms >= 0){
            start = std::chrono::high_resolution_clock::now();
        }
        // tag format 24 bits rank_self_in_peer 24 bits rank_peer 16 bits tag
        uint64_t aux_rank_peer = request.m_comm.rank_peer;
        uint64_t aux_rank_self_in_peer = request.m_comm.rank_self_in_peer;
        uint64_t aux_tag = tag;
        uint64_t tag_recv = (aux_rank_self_in_peer << 40) | (aux_rank_peer << 16) | aux_tag;

        if (request.m_comm.rank_peer == LFI_ANY_COMM_SHM || request.m_comm.rank_peer == LFI_ANY_COMM_PEER)
        {
            // mask = 0x0000'00FF'FFFF'0000;
            // mask = 0xFFFF'FF00'0000'0000;
            mask = 0xFFFF'FFFF'FFFF'0000;
        }

        debug_info("[LFI] Start size " << size << " rank_peer " << request.m_comm.rank_peer << " rank_self_in_peer " << request.m_comm.rank_self_in_peer << " tag " << tag << " recv_context " << (void *)&request.context);

        fid_ep *p_rx_ep = request.m_comm.m_ep.use_scalable_ep ? request.m_comm.m_ep.rx_ep : request.m_comm.m_ep.ep;
        request.wait_context = true;
        request.is_send = false;
        do
        {
            {
                std::unique_lock global_lock(request.m_comm.m_ep.mutex_send_recv);
                ret = fi_trecv(p_rx_ep, buffer, size, NULL, request.m_comm.fi_addr, tag_recv, mask, &request.context);
            }

            if (ret == -FI_EAGAIN){
                std::unique_lock global_lock(request.m_comm.m_ep.mutex_ep, std::defer_lock);
                if (global_lock.try_lock()){
                    progress(request);
                    global_lock.unlock();
                }
                
                if (timeout_ms >= 0){
                    int32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count(); 
                    if (elapsed_ms >= timeout_ms){
                        msg.error = -LFI_TIMEOUT;
                        return msg;
                    }
                }

                if (request.m_comm.is_canceled){
                    msg.error = -LFI_CANCELED_COMM;
                    return msg;
                }
            }
        } while (ret == -FI_EAGAIN);

        if (ret != 0)
        {
            printf("error posting recv buffer (%d)\n", ret);
            msg.error = -LFI_ERROR;
            return msg;
        }

        debug_info("[LFI] Waiting on rank_peer " << request.m_comm.rank_peer);

        if (env::get_instance().LFI_fault_tolerance){
            std::unique_lock lock(request.m_comm.ft_mutex);
            debug_info("[LFI] insert request "<<std::hex<<&request<<std::dec<<" in comm "<<request.m_comm.rank_peer);
            request.m_comm.ft_requests.insert(&request);
        }

        msg.size = size;
        msg.tag = tag_recv & 0x0000'0000'0000'FFFF;
        msg.rank_self_in_peer = (tag_recv & 0xFFFF'FF00'0000'0000) >> 40;
        msg.rank_peer = (tag_recv & 0x0000'00FF'FFFF'0000) >> 16;

        debug_info("[LFI] msg size " << msg.size << " rank_peer " << msg.rank_peer << " rank_self_in_peer " << msg.rank_self_in_peer << " tag " << msg.tag << " error " << msg.error);
        debug_info("[LFI] End = " << size);
        return msg;
    }

    fabric_msg LFI::recv_peek(uint32_t comm_id, void *buffer, size_t size, uint32_t tag)
    {
        int ret;
        fabric_msg msg = {};

        // Check if comm exists
        fabric_comm *comm = get_comm(comm_id);
        if (comm == nullptr){
            msg.error = -LFI_COMM_NOT_FOUND;
            return msg;
        }
        fabric_request request(*comm);

        // Check cancelled comm
        if (request.m_comm.is_canceled){
            msg.error = -LFI_CANCELED_COMM;
            return msg;
        }

        request.reset();
        uint64_t mask = 0;
        // tag format 24 bits rank_self_in_peer 24 bits rank_peer 16 bits tag
        uint64_t aux_rank_peer = request.m_comm.rank_peer;
        uint64_t aux_rank_self_in_peer = request.m_comm.rank_self_in_peer;
        uint64_t aux_tag = tag;
        uint64_t tag_recv = (aux_rank_self_in_peer << 40) | (aux_rank_peer << 16) | aux_tag;

        if (request.m_comm.rank_peer == LFI_ANY_COMM_SHM || request.m_comm.rank_peer == LFI_ANY_COMM_PEER)
        {
            // mask = 0x0000'00FF'FFFF'0000;
            // mask = 0xFFFF'FF00'0000'0000;
            mask = 0xFFFF'FFFF'FFFF'0000;
        }

        debug_info("[LFI] Start size " << size << " rank_peer " << request.m_comm.rank_peer << " rank_self_in_peer " << request.m_comm.rank_self_in_peer << " tag " << tag << " recv_context " << (void *)&request.context);

        fid_ep *p_rx_ep = request.m_comm.m_ep.use_scalable_ep ? request.m_comm.m_ep.rx_ep : request.m_comm.m_ep.ep;
        request.wait_context = true;
        iovec iov = {
            .iov_base=buffer,
            .iov_len=size,
        };
        fi_msg_tagged  msg_to_peek = { 
            .msg_iov = &iov,
            .desc = nullptr,
            .iov_count = 1,
            .addr = request.m_comm.fi_addr,
            .tag = tag_recv,
            .ignore = mask,
            .context = &request.context,
            .data = 0,
        };
        // First we PEEK with CLAIM to only generate one match
        do
        {
            {
                std::unique_lock global_lock(request.m_comm.m_ep.mutex_send_recv);
                ret = fi_trecvmsg(p_rx_ep, &msg_to_peek, FI_PEEK | FI_CLAIM);
            }

            if (ret == -FI_EAGAIN){
                std::unique_lock global_lock(request.m_comm.m_ep.mutex_ep, std::defer_lock);
                if (global_lock.try_lock()){
                    progress(request);
                    global_lock.unlock();
                }
                
                if (request.m_comm.is_canceled){
                    msg.error = -LFI_CANCELED_COMM;
                    return msg;
                }
            }
        } while (ret == -FI_EAGAIN);

        if (ret != 0)
        {
            printf("error posting recv buffer (%d)\n", ret);
            msg.error = -LFI_ERROR;
            return msg;
        }

        debug_info("[LFI] Waiting for " << request.to_string());

        ret = wait(request);
        if (ret != 0)
        {
            printf("error waiting recv peek (%d)\n", ret);
            msg.error = -LFI_ERROR;
            return msg;
        }
        // If the PEEK request is successfully we need to claim the content
        if (request.error == 0){
            debug_info("[LFI] successfully PEEK, now CLAIM data");
            do
            {
                {
                    std::unique_lock global_lock(request.m_comm.m_ep.mutex_send_recv);
                    ret = fi_trecvmsg(p_rx_ep, &msg_to_peek, FI_CLAIM);
                }

                if (ret == -FI_EAGAIN){
                    std::unique_lock global_lock(request.m_comm.m_ep.mutex_ep, std::defer_lock);
                    if (global_lock.try_lock()){
                        progress(request);
                        global_lock.unlock();
                    }
                    
                    if (request.m_comm.is_canceled){
                        msg.error = -LFI_CANCELED_COMM;
                        return msg;
                    }
                }
            } while (ret == -FI_EAGAIN);

            if (ret != 0)
            {
                printf("error posting recv buffer (%d)\n", ret);
                msg.error = -LFI_ERROR;
                return msg;
            }

            ret = wait(request);
            if (ret != 0)
            {
                printf("error waiting recv claim (%d)\n", ret);
                msg.error = -LFI_ERROR;
                return msg;
            }
        }

        msg.size = size;
        msg.error = request.error;
        msg.tag = request.entry.tag & 0x0000'0000'0000'FFFF;
        msg.rank_self_in_peer = (request.entry.tag & 0xFFFF'FF00'0000'0000) >> 40;
        msg.rank_peer = (request.entry.tag & 0x0000'00FF'FFFF'0000) >> 16;

        debug_info("[LFI] msg size " << msg.size << " rank_peer " << msg.rank_peer << " rank_self_in_peer " << msg.rank_self_in_peer << " tag " << msg.tag << " error " << msg.error);
        debug_info("[LFI] End = " << size);
        return msg;
    }
} // namespace LFI