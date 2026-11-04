
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
// #define ONLY_DEBUG
// #define DEBUG_SENDS
#include "ld_preload.hpp"

#include <poll.h>
#include <execinfo.h>
#include <sys/eventfd.h>

#include "lfi.hpp"
#include "proxy.hpp"
#include "debug.hpp"

#include "impl/socket.hpp"
#include "impl/env.hpp"
#include "impl/ns.hpp"

using namespace LFI;

constexpr static ssize_t msghdr_MIN = 16;

void print_backtrace()
{
    void *buffer[100];
    int nptrs = backtrace(buffer, 100);

    char **symbols = backtrace_symbols(buffer, nptrs);
    if (symbols == nullptr)
    {
        std::cerr << "Error backtrace_symbols" << std::endl;
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < nptrs; i++)
    {
        std::cout << symbols[i] << std::endl;
    }

    free(symbols);
}


std::ostream &operator<<(std::ostream &os, const ld_preload::lfi_socket& lfi_socket)
{
    os << "lfi_socket: comm "<<lfi_socket.lfi_id<<" eventfd "<<lfi_socket.eventfd << " ";
    for (auto &buff : lfi_socket.buffers)
    {
        os << "buff[" << buff.consumed << ":" << buff.buffer.size() << "] ";
    }
    if (lfi_socket.buffers.size() == 0) {
        os << "empty";
    }
    return os;
}

bool ld_preload::is_caller_libfabric()
{
    void *buffer[100];
    int nptrs = backtrace(buffer, 100);

    char **symbols = backtrace_symbols(buffer, nptrs);
    if (symbols == nullptr)
    {
        std::cerr << "Error backtrace_symbols" << std::endl;
        exit(EXIT_FAILURE);
    }

    bool out = false;
    // for (int i = 0; i < nptrs; i++)
    // {
    //     if (std::string_view(symbols[i]).find("libmpi.so") != std::string_view::npos)
    //     {
    //         out = false;
    //         break;
    //     }
    // }
    // print_backtrace();

    if (out == false) {
        for (int i = 0; i < nptrs; i++)
        {
            if (std::string_view(symbols[i]).find("libpsm2.so") != std::string_view::npos ||
                std::string_view(symbols[i]).find("libfabric.so") != std::string_view::npos ||
                std::string_view(symbols[i]).find("libpmix.so") != std::string_view::npos)
            {
                out = true;
                break;
            }
        }
    }

    free(symbols);
    return out;
}

void ld_preload::thread_eventfd_start()
{
    debug("[LFI LD_PRELOAD] Start");
    {
        std::lock_guard lock(m_thread_eventfd_mutex);
        if (m_thread_eventfd_is_running)
            return;
        m_thread_eventfd_is_running = true;
    }
    for (size_t i = 0; i < env::get_instance().LFI_ld_preload_threads; i++)
    {
        std::thread new_thread(thread_eventfd_loop, i);
        m_thread_eventfd.emplace_back(std::move(new_thread));
        m_shm_request.emplace_back();
        m_peer_request.emplace_back();
    }
    debug("[LFI LD_PRELOAD] End");
    return;
}

void ld_preload::thread_eventfd_end()
{
    debug("[LFI LD_PRELOAD] Start");
    {
        std::lock_guard lock(m_thread_eventfd_mutex);
        if (!m_thread_eventfd_is_running)
            return;
        m_thread_eventfd_is_running = false;

        for (size_t i = 0; i < env::get_instance().LFI_ld_preload_threads; i++) {
            m_shm_request[i]->cancel();
            m_peer_request[i]->cancel();
        }
    }

    for (auto &thread : m_thread_eventfd)
    {
        thread.join();
    }
    debug("[LFI LD_PRELOAD] End");
    return;
}

struct noti_buff {
    uint32_t size{0};
    uint32_t tag{0};
};

void ld_preload::thread_eventfd_loop(int id)
{
    debug("[LFI LD_PRELOAD] Start");
    auto &ld_preload = ld_preload::get_instance();
    auto &lfi = ld_preload.m_lfi;
    std::vector<noti_buff> noti_buffered(1024, noti_buff{});
    std::vector<noti_buff> aux_buff_shm(1024, noti_buff{});
    std::vector<noti_buff> aux_buff_peer(1024, noti_buff{});
    auto any_recv = [&](::LFI::lfi_request& req, bool is_shm){
        int ret;
        if (is_shm){
            ret = lfi->async_recv(aux_buff_shm.data(), aux_buff_shm.size()*sizeof(aux_buff_shm[0]), LFI_TAG_RECV_LD_PRELOAD, req);
        }else{
            ret = lfi->async_recv(aux_buff_peer.data(), aux_buff_peer.size()*sizeof(aux_buff_peer[0]), LFI_TAG_RECV_LD_PRELOAD, req);
        }
        if (ret < 0){
            print("Error in async_recv")
            return -1;
        }
        return 0;
    };
    std::vector<std::unique_ptr<::LFI::lfi_request>> recv_requests;
    std::vector<::LFI::lfi_request*> ref_requests;
    while (ld_preload.m_thread_eventfd_is_running)
    {
        if (!ld_preload.m_shm_request[id]){
            auto comm = lfi->get_comm(::LFI::ANY_COMM_SHM);
            if (!comm){
                print("Error get_comm ANY_COMM_SHM");
                continue;
            }
            ld_preload.m_shm_request[id] = std::make_unique<::LFI::lfi_request>(comm->m_endpoint, comm);
            if (!ld_preload.m_shm_request[id]){
                print("Error shm_request is null");
                continue;
            }
            
            if (any_recv(*ld_preload.m_shm_request[id], true)){
                print_error("shm any_recv");
            }
        }

        if (!ld_preload.m_peer_request[id]){
            auto comm = lfi->get_comm(::LFI::ANY_COMM_PEER);
            if (!comm){
                print("Error get_comm ANY_COMM_PEER");
                continue;
            }
            ld_preload.m_peer_request[id] = std::make_unique<::LFI::lfi_request>(comm->m_endpoint, comm);
            if (!ld_preload.m_peer_request[id]){
                print("Error peer_request is null");
                continue;
            }

            if (any_recv(*ld_preload.m_peer_request[id], false)){
                print_error("peer any_recv");
            }
        }

        ::LFI::lfi_request* requests[2] = {ld_preload.m_shm_request[id].get(), ld_preload.m_peer_request[id].get()};
        int completed = lfi->wait_num(requests, 2, 1);
        int source = -1;
        uint64_t how_many = 0;
        if (completed == 0) {
            // SHM
            source = ld_preload.m_shm_request[id]->error < 0 ? ld_preload.m_shm_request[id]->error : ld_preload.m_shm_request[id]->source;
            how_many = ld_preload.m_shm_request[id]->size / sizeof(aux_buff_shm[0]);
            noti_buffered.swap(aux_buff_shm);
            // Reuse the request
            if (any_recv(*ld_preload.m_shm_request[id], true)){
                print_error("peer any_recv");
            }
        } else if (completed == 1) {
            // PEER
            source = ld_preload.m_peer_request[id]->error < 0 ? ld_preload.m_peer_request[id]->error : ld_preload.m_peer_request[id]->source;
            how_many = ld_preload.m_peer_request[id]->size / sizeof(aux_buff_peer[0]);
            noti_buffered.swap(aux_buff_peer);
            // Reuse the request
            if (any_recv(*ld_preload.m_peer_request[id], false)){
                print_error("peer any_recv");
            }
        } else if (completed == -LFI_TIMEOUT) {
            continue;
        } else {
            print_error("lfi wait_num");
            continue;
        }
        if (source < 0) {
            if (ld_preload.m_thread_eventfd_is_running) {
                print_error("lfi error: " << source << " " << ::LFI::lfi_strerror(source));
            }
            continue;
        }

        #ifdef DEBUG
        for (size_t i = 0; i<how_many;i++) {
            debug("[LFI LD_PRELOAD] Recv msg source "<<source<<" how_many "<<how_many<<" ["<<i<<"] = "<<noti_buffered[i]);
        }
        #endif
        int socket = -1;
        {
            std::unique_lock queue_lock(ld_preload.m_map_comm_socket_mutex);
            debug("Find m_map_comm_socket: "<<source);
            #ifdef DEBUG
            for (auto&&[comm, socket]: ld_preload.m_map_comm_socket){
                debug("m_map_comm_socket["<<comm<<"]"<<socket);
            }
            #endif
            auto it = ld_preload.m_map_comm_socket.find(source);
            if (it == ld_preload.m_map_comm_socket.end()){
                print_error("Error find the comm "<<source);
                continue;
            }
            socket = it->second;
        }

        ld_preload::lfi_socket* lfi_sock = nullptr;
        {
            std::unique_lock queue_lock(ld_preload.m_mutex);
            debug("Find lfi_socket: "<<socket);
            #ifdef DEBUG
            for (auto&&[comm, socket]: ld_preload.socket_ids){
                debug("ld_preload.socket_ids["<<comm<<"]"<<socket);
            }
            #endif
            auto it = ld_preload.socket_ids.find(socket);
            if (it == ld_preload.socket_ids.end()){
                print_error("Error find the socket "<<socket);
                continue;
            }
            lfi_sock = it->second.get();
        }

        auto& lfi_socket = *lfi_sock;
        debug("Find comm: "<<lfi_socket.lfi_id);
        auto comm = lfi->get_comm(lfi_socket.lfi_id);
        if (!comm){
            print_error("Error find the comm "<<source);
            continue;
        }

        recv_requests.clear();
        ref_requests.clear();

        uint64_t buff_size = 0;
        for (size_t i = 0; i < how_many; i++)
        {
            recv_requests.emplace_back(std::make_unique<::LFI::lfi_request>(comm->m_endpoint, comm));
            ref_requests.emplace_back(recv_requests[i].get());
            buff_size += noti_buffered[i].size;
        }
        
        debug_send("Thread recvmsg comm "<<comm->rank_self_in_peer<<":"<<comm->rank_peer<<" size "<<buff_size);
        debug("[LFI LD_PRELOAD] Recv notification of "<<how_many<<" msgs of size "<<buff_size);
        lfi_socket::buffered new_buffer;
        new_buffer.buffer.resize(buff_size, 0);

        uint64_t already_recv = 0;
        int error = 0;
        for (size_t i = 0; i < how_many; i++)
        {
            auto ret = lfi->async_recv(new_buffer.buffer.data()+already_recv, noti_buffered[i].size, noti_buffered[i].tag, *recv_requests[i]);
            if (ret < 0){
                print_error("Error recv buffered in comm "<<lfi_socket.lfi_id<<" error "<<ret);
                error = 1;
                break;
            }
            debug("[LFI LD_PRELOAD] Async recv of msg of size "<<noti_buffered[i].size<<" tag "<<noti_buffered[i].tag);
            already_recv += noti_buffered[i].size;
        }
        if (error != 0){
            continue;
        }
        ssize_t ret;
        ret = lfi->wait_num(ref_requests.data(), ref_requests.size(), ref_requests.size());
        if (ret < 0) {
            print_error("Error waiting recvs buffered in comm "<<lfi_socket.lfi_id<<" error "<<ret);
            continue;
        }

        std::unique_lock eventfd_lock(lfi_socket.m_mutex);
        lfi_socket.buffers.emplace_back(std::move(new_buffer));
        debug("[LFI LD_PRELOAD] Actual msg in queue "<<lfi_socket.buffers.size());
        uint64_t buff = 1;
        debug("[LFI LD_PRELOAD] Notify "<<lfi_socket);
        ret = PROXY(write)(lfi_socket.eventfd, &buff, sizeof(buff));
        if (ret < 0){
            debug("[LFI LD_PRELOAD] Error writing eventfd "<<lfi_socket.eventfd<<" error "<<ret<<" "<<strerror(errno));
            continue;
        }
        
        debug("[LFI LD_PRELOAD] msg queue "<<lfi_socket);
    }
    debug("[LFI LD_PRELOAD] End");
}

int ld_preload::create_eventfd(lfi_socket &ids)
{
    auto new_eventfd = eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE | EFD_NONBLOCK);
    if (new_eventfd < 0)
    {
        return new_eventfd;
    }
    ids.eventfd = new_eventfd;

    debug("[LFI LD_PRELOAD] save eventfd " << new_eventfd << " in lfi_ids");
    
    debug("[LFI LD_PRELOAD] async_recv fot eventfd " << new_eventfd);
    return new_eventfd;
}

int ld_preload::destroy_eventfd(lfi_socket &ids)
{
    if (ids.eventfd < 0) return 0;

    auto ret = PROXY(close)(ids.eventfd);
    if (ret < 0)
    {
        print("Error close the eventfd "<<ids.eventfd);
    }
    debug("[LFI LD_PRELOAD] remove eventfd " << ids.eventfd << " in lfi_ids");

    ids.eventfd = -1;
    return 0;
}

ssize_t ld_preload::internal_recvmsg(lfi_socket& lfi_socket, const struct iovec *iov, size_t count) {
    debug("[LFI LD_PRELOAD] "<<lfi_socket);
    auto comm_id = lfi_socket.lfi_id;
    if (comm_id == -1)
    {
        debug("[LFI LD_PRELOAD] comm_id is -1");
        return -1;
    }

    auto comm = m_lfi->get_comm(comm_id);
    if (comm == nullptr){
        debug("[LFI LD_PRELOAD] comm is nullptr");
        return -1;
    }

    ssize_t ret = 0;
    std::unique_lock lock(lfi_socket.m_mutex);
    if (lfi_socket.buffers.size() == 0){
        debug("[LFI LD_PRELOAD] msg_size empty");  
        errno = EAGAIN;
        return -1;
    }
    debug("lfi_socket "<<lfi_socket<<" count "<<count<<printIovec(iov, count));
    auto& buff = lfi_socket.buffers.front();
    for (size_t i = 0; i < count; i++)
    {
        if (buff.buffer.size() == buff.consumed){
            break;
        }
        auto& actual_iov = iov[i];
        debug("[LFI LD_PRELOAD] start msg queue "<<lfi_socket);

        auto to_copy = std::min(actual_iov.iov_len, (buff.buffer.size()-buff.consumed));

        std::memcpy(actual_iov.iov_base, (buff.buffer.data()+buff.consumed), to_copy);

        ret += to_copy;
        buff.consumed += to_copy;
        debug("[LFI LD_PRELOAD] end msg queue "<<lfi_socket);
    }
    
    if (buff.buffer.size() == buff.consumed){
        lfi_socket.buffers.pop_front();
        uint64_t dummy = 0; 
        ssize_t ret_write = PROXY(read)(lfi_socket.eventfd, &dummy, sizeof(dummy));
        if (ret_write < 0){
            debug("[LFI LD_PRELOAD] Error write in eventfd "<<lfi_socket.eventfd<<" err "<<ret_write<<" "<<strerror(errno));  
        }
    }

    debug_send("Recvmsg comm "<<comm->rank_self_in_peer<<":"<<comm->rank_peer<<" size "<<ret);
    debug("[LFI LD_PRELOAD] msg queue "<<lfi_socket);
    return ret;
}

ssize_t ld_preload::internal_sendmsg(lfi_socket& lfi_socket, const struct iovec *iov, size_t count) {
    ssize_t ret = 0;
    if (count == 0){
        debug("[LFI LD_PRELOAD] msg_iovlen 0");
        return ret;
    }
    auto comm_id = lfi_socket.lfi_id;
    if (comm_id == -1){
        debug("[LFI LD_PRELOAD] Error comm_id -1");
        return -1;
    }
    debug("[LFI LD_PRELOAD] "<<lfi_socket);

    auto comm = m_lfi->get_comm(comm_id);
    if (comm == nullptr){
        debug("[LFI LD_PRELOAD] Error comm not found");
        return -1;
    }
    
    // [size,[index...]]...
    struct buff_groups{
        uint64_t size = 0;
        std::vector<uint64_t> list = {};
    };
    std::vector<buff_groups> groups;
    uint64_t index = 0;
    groups.emplace_back();
    groups[index].list.reserve(count); 
    for (size_t i = 0; i < count; i++)
    {
        // If the actual fits in the group
        if (groups[index].size != 0 && (groups[index].size + iov[i].iov_len) > env::get_instance().LFI_ld_preload_buffered) {
            groups.emplace_back();
            index++;
            groups[index].list.reserve(count - i); 
        }
        groups[index].list.emplace_back(i);
        groups[index].size += iov[i].iov_len;
    #ifdef DEBUG
        std::cerr<<"[LFI LD_PRELOAD] actual_group size "<<groups[index].size<<" list[";
        for (auto &index : groups[index].list)
        {
            std::cerr<<index<<" ";
        }
        std::cerr<<"] size "<<groups[index].list.size()<<std::endl;
    #endif
    }
    debug("[LFI LD_PRELOAD] calcule buffering for iov of "<<count<<" msgs to "<<groups.size());
    size_t size_to_buffer = 0;
    for (auto &group : groups)
    {
        if (group.list.size() != 1){
            size_to_buffer += group.size;
        }

        debug("[LFI LD_PRELOAD] group size "<<group.size<<" group list size "<<group.list.size());
    }
    debug("[LFI LD_PRELOAD] total buffering for "<<groups.size()<<" msgs "<<size_to_buffer);
    
    std::vector<uint8_t> buffered;
    buffered.resize(size_to_buffer, 0);
    size_t actual_size = 0;

    std::vector<iovec> v_iov;
    v_iov.reserve(groups.size());
    for (auto &group : groups)
    {
        debug("[LFI LD_PRELOAD] group size "<<group.size<<" group list size "<<group.list.size());
        if (group.list.size() == 1){
            [[maybe_unused]] auto& io = v_iov.emplace_back(iovec{
                .iov_base = iov[group.list[0]].iov_base,
                .iov_len = iov[group.list[0]].iov_len,
            });
            debug("[LFI LD_PRELOAD] not buffering "<<io.iov_len<<" origin size "<<iov[group.list[0]].iov_len);
        }else{
            size_t start_buff = actual_size;
            for (auto &index : group.list)
            {
                std::memcpy(buffered.data()+actual_size, iov[index].iov_base, iov[index].iov_len);
                actual_size += iov[index].iov_len;
            }
            [[maybe_unused]] auto& io = v_iov.emplace_back(iovec{
                .iov_base = reinterpret_cast<void*>(buffered.data() + start_buff),
                .iov_len = group.size,
            });
            debug("[LFI LD_PRELOAD] buffering "<<io.iov_len<<" group size "<<group.size);
        }
    }
    
    std::vector<std::unique_ptr<::LFI::lfi_request>> requests;
    std::vector<::LFI::lfi_request*> ref_requests;
    requests.reserve(v_iov.size());
    ref_requests.reserve(v_iov.size());

    uint64_t to_send = 0;
    std::vector<noti_buff> noti_buffered;
    noti_buffered.reserve(v_iov.size());
    static std::atomic<int> static_counter;
    for (size_t i = 0; i < v_iov.size(); i++)
    {
        requests.emplace_back(std::make_unique<::LFI::lfi_request>(comm->m_endpoint, comm));
        ref_requests.emplace_back(requests[i].get());

        auto current_count = static_counter.fetch_add(1);
        uint32_t tag = LFI_TAG_BUFFERED_LD_PRELOAD + (current_count % 1000001);

        noti_buffered.emplace_back(noti_buff{.size = static_cast<uint32_t>(v_iov[i].iov_len), .tag=tag});
        to_send += v_iov[i].iov_len;
    }
    
    debug("[LFI LD_PRELOAD] Send notification of "<<v_iov.size()<<" msgs");
    auto msg_not = m_lfi->send(comm_id, noti_buffered.data(), noti_buffered.size()*sizeof(noti_buffered[0]), LFI_TAG_RECV_LD_PRELOAD);
    if (msg_not < 0){
        debug("[LFI LD_PRELOAD] Error in send notification "<<msg_not<<" "<<::LFI::lfi_strerror(msg_not));
        return msg_not;
    }
    debug("[LFI LD_PRELOAD] Sended notification of "<<v_iov.size()<<" msgs");


    for (size_t i = 0; i < v_iov.size(); i++)
    {
        debug("[LFI LD_PRELOAD] Start async send");
        auto ret = m_lfi->async_send(v_iov[i].iov_base, v_iov[i].iov_len, noti_buffered[i].tag, *requests[i]);
        if (ret < 0){
            debug("[LFI LD_PRELOAD] Error in async send "<<ret<<" "<<::LFI::lfi_strerror(ret));
            return ret;
        }
    }
    
    ret = m_lfi->wait_num(ref_requests.data(), ref_requests.size(), ref_requests.size());
    if (ret < 0){
        debug("[LFI LD_PRELOAD] Error in wait sends "<<ret<<" "<<::LFI::lfi_strerror(ret));
        return ret;
    }
    debug_send("Sendmsg comm "<<comm->rank_peer<<":"<<comm->rank_self_in_peer<<" size "<<to_send);

    return to_send;
}

#ifdef __cplusplus
extern "C"
{
#endif
    int socket(int domain, int type, int protocol) throw()
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(socket)(domain, type, protocol);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << socket_str(domain, type, protocol) << ")");
        auto ret = PROXY(socket)(domain, type, protocol);
        debug("[LFI LD_PRELOAD] End (" << socket_str(domain, type, protocol) << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << socket_str(domain, type, protocol) << ")");
        auto ret = PROXY(socket)(domain, type, protocol);
        if (ret < 0)
        {
            return ret;
        }
        if (type == SOCK_STREAM)
        {
            auto &preload = ld_preload::get_instance();
            std::unique_lock lock(preload.m_mutex);
            preload.socket_ids.emplace(std::piecewise_construct, std::forward_as_tuple(ret), std::forward_as_tuple(std::make_unique<ld_preload::lfi_socket>()));
            debug("[LFI LD_PRELOAD] save fd " << ret << " in socket_ids");
        }
        debug("[LFI LD_PRELOAD] End (" << socket_str(domain, type, protocol) << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(connect)(sockfd, addr, addrlen);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(connect)(sockfd, addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(connect)(sockfd, addr, addrlen);
        auto save_errno = errno;
        if (ret == -1 && errno != EINPROGRESS){
            return ret;
        }

        auto &preload = ld_preload::get_instance();
        
        {
            std::unique_lock lock(preload.m_mutex);
            auto it_socket = preload.socket_ids.find(sockfd);
            if (it_socket == preload.socket_ids.end())
            {
                debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
                return ret;
            }
        }
        // std::this_thread::sleep_for(std::chrono::milliseconds(500));
        // print("sockfd "<<sockfd<<" "<<STR_ERRNO(ret));
        size_t buff = 123;
        int ret1 = 0;
        do{
            ret1 = socket::send(sockfd, &buff, sizeof(buff));
        }while(ret1 < 0 && errno == EAGAIN);
        // print("send "<<ret1<<" "<<STR_ERRNO(ret));
        int ret2 = 0;
        do{
            // print("do recv");
            ret2 = socket::recv(sockfd, &buff, sizeof(buff));
            // print("recv "<<ret2<<" "<<strerror(errno));
            // std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }while(ret2 < 0 && errno == EAGAIN);

        std::string server_addr;
        do{
            ret2 = socket::recv_str(sockfd, server_addr);
        }while(ret2 < 0 && errno == EAGAIN);

        debug("[LFI LD_PRELOAD] recved host_ip "<<server_addr);
        debug("Try connect to " << server_addr);

        int client_socket = socket::client_init(server_addr, ::LFI::env::get_instance().LFI_port, true);
        if (client_socket < 0)
        {
            print_error("socket::client_init (" << server_addr << ", " << ::LFI::env::get_instance().LFI_port << ")");
        }
        std::unique_lock lock(preload.m_map_comm_socket_mutex);
        auto new_fd = preload.m_lfi->reserve_comm();
        auto ret_init = preload.m_lfi->init_client(client_socket, new_fd);


        // TODO handle error in close
        socket::close(client_socket);
        {
            std::unique_lock lock(preload.m_mutex);
            preload.socket_ids[sockfd]->lfi_id = new_fd;        
            if (ret_init < 0) {
                ret = ret_init;
                debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO(ret));
                return ret;
            }
            debug("[LFI LD_PRELOAD] save sockfd " << sockfd << " with lfi_id " << new_fd);
            preload.m_map_comm_socket.emplace(std::piecewise_construct, std::forward_as_tuple(new_fd), std::forward_as_tuple(sockfd));
            preload.create_eventfd(*preload.socket_ids[sockfd]);
            errno = save_errno;
            debug("[LFI LD_PRELOAD] "<<*preload.socket_ids[sockfd]);
        }
        
        char aux_buf[1024] = "Hello";
        for (size_t i = 0; i < 10; i++){
            debug("[LFI LD_PRELOAD] send aux_buf");
            auto aux_ret = preload.m_lfi->send(new_fd, aux_buf, sizeof(aux_buf), 1);
            if (aux_ret < 0) {
                print_error("connect send lfi (" << server_addr << ", " << ::LFI::env::get_instance().LFI_port << ")");
            }
            debug("[LFI LD_PRELOAD] recv aux_buf");
            aux_ret = preload.m_lfi->recv(new_fd, aux_buf, sizeof(aux_buf), 1);
            if (aux_ret < 0) {
                print_error("connect recv lfi (" << server_addr << ", " << ::LFI::env::get_instance().LFI_port << ")");
            }
        }

        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    int accept(int sockfd, struct sockaddr *__restrict addr, socklen_t *__restrict addrlen)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return accept4(sockfd, addr, addrlen, 0);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(accept)(sockfd, addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = accept4(sockfd, addr, addrlen, 0);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    int accept4(int sockfd, struct sockaddr *__restrict addr, socklen_t *__restrict addrlen, int flags)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(accept4)(sockfd, addr, addrlen, flags);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ")");
        auto ret = PROXY(accept4)(sockfd, addr, addrlen, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ")");
        auto ret = PROXY(accept4)(sockfd, addr, addrlen, flags);
        debug("[LFI LD_PRELOAD] Accept4 ret = "<<ret);
        if (ret < 0) return ret;

        auto &preload = ld_preload::get_instance();
        {
            std::unique_lock lock(preload.m_mutex);
            auto it_socket = preload.socket_ids.find(sockfd);
            if (it_socket == preload.socket_ids.end())
            {
                debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
                return ret;
            }
        }

        std::string host_ip = ::LFI::ns::get_host_ip();
        int server_socket = socket::server_init("", ::LFI::env::get_instance().LFI_port);
        if (server_socket < 0){
            return server_socket;
        }

        // print("sockfd "<<ret<<" "<<STR_ERRNO(ret));
        size_t buff = 123;
        int ret2 = 0;
        do{
            // print("do recv");
            ret2 = socket::recv(ret, &buff, sizeof(buff));
            // print("recv "<<ret2<<" "<<strerror(errno));
            // std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }while(ret2 < 0 && errno == EAGAIN);
        int ret1 = 0;
        do{
            ret1 = socket::send(ret, &buff, sizeof(buff));
        }while(ret1 < 0 && errno == EAGAIN);
        // print("send "<<ret1<<" "<<STR_ERRNO(ret));
        
        do{
            ret1 = socket::send_str(ret, host_ip);
        }while(ret1 < 0 && errno == EAGAIN);
        debug("[LFI LD_PRELOAD] sended host_ip "<<host_ip);


        int client_socket = socket::accept(server_socket);
        if (client_socket < 0)
        {
            print_error("socket::client_init (" << server_socket << ")");
        }
        
        std::unique_lock lock(preload.m_map_comm_socket_mutex);
        
        auto new_fd = preload.m_lfi->reserve_comm();
        auto ret_init = preload.m_lfi->init_server(client_socket, new_fd);


        // TODO handle error in close
        socket::close(client_socket);
        socket::close(server_socket);
        if (ret_init < 0) {
            ret = ret_init;
            debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO(ret));
            return ret;
        }

        {
            std::unique_lock lock(preload.m_mutex);
            auto it = preload.socket_ids.emplace(std::piecewise_construct, std::forward_as_tuple(ret), std::forward_as_tuple(std::make_unique<ld_preload::lfi_socket>()));
            auto& buff = it.first->second;
            buff->lfi_id = new_fd;
            preload.create_eventfd(*preload.socket_ids[ret]);
        }
        preload.m_map_comm_socket.emplace(std::piecewise_construct, std::forward_as_tuple(new_fd), std::forward_as_tuple(ret));
        debug("[LFI LD_PRELOAD] save new lfi_socket fd " << ret << " lfi " << new_fd);

        
        char aux_buf[1024] = "Hello";
        for (size_t i = 0; i < 10; i++){
            debug("[LFI LD_PRELOAD] recv aux_buf");
            auto aux_ret = preload.m_lfi->recv(new_fd, aux_buf, sizeof(aux_buf), 1);
            if (aux_ret < 0) {
                print_error("accept recv lfi (" << host_ip << ", " << ::LFI::env::get_instance().LFI_port << ")");
            }
            debug("[LFI LD_PRELOAD] send aux_buf");
            aux_ret = preload.m_lfi->send(new_fd, aux_buf, sizeof(aux_buf), 1);
            if (aux_ret < 0) {
                print_error("accept send lfi (" << host_ip << ", " << ::LFI::env::get_instance().LFI_port << ")");
            }
        }

        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    int close(int fd)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(close)(fd);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << fd << ")");
        int ret = PROXY(close)(fd);
        debug("[LFI LD_PRELOAD] End (" << fd << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << fd << ")");
        int ret = PROXY(close)(fd);

        auto &preload = ld_preload::get_instance();
        ld_preload::lfi_socket* lfi_sock = nullptr;
        {
            std::unique_lock lock(preload.m_mutex);
            auto it_socket = preload.socket_ids.find(fd);
            if (it_socket != preload.socket_ids.end()) {
                lfi_sock = (*it_socket).second.get();
            }
        }
        if (!lfi_sock)
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
        } else {
            debug("[LFI LD_PRELOAD] close "<<*lfi_sock);
            if (lfi_sock->lfi_id != -1){
                ret = preload.m_lfi->close_comm(lfi_sock->lfi_id);
            }
            {
                std::unique_lock lock(preload.m_map_comm_socket_mutex);
                preload.m_map_comm_socket.erase(lfi_sock->lfi_id);
            }
            preload.destroy_eventfd(*lfi_sock);
            {
                std::unique_lock lock(preload.m_mutex);
                preload.socket_ids.erase(fd);
            }
        }
        debug("[LFI LD_PRELOAD] End (" << fd << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    ssize_t write(int fd, const void *buf, size_t count)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(write)(fd, buf, count);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << fd << ", " << buf << ", " << count << ")");
        auto ret = PROXY(write)(fd, buf, count);
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << buf << ", " << count << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << fd << ", " << buf << ", " << count << ")");
        ssize_t ret = 0;
        auto &preload = ld_preload::get_instance();
        ld_preload::lfi_socket* lfi_sock = nullptr;

        {
            std::unique_lock lock(preload.m_mutex);
            auto it_socket = preload.socket_ids.find(fd);
            if (it_socket != preload.socket_ids.end()){
                lfi_sock = it_socket->second.get();
            }
        }
        if (!lfi_sock)
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
            ret = PROXY(write)(fd, buf, count);
        }
        else
        {
            iovec iov = {.iov_base = const_cast<void *>(buf), .iov_len = count};
            int iovcnt = 1;
            ret = preload.internal_sendmsg(*lfi_sock, &iov, iovcnt);
        }
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << buf << ", " << count << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(writev)(fd, iov, iovcnt);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << fd << ", " << printIovec(iov, iovcnt, 1024) << ", " << iovcnt << ")");
        auto ret = PROXY(writev)(fd, iov, iovcnt);
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << printIovec(iov, iovcnt) << ", " << iovcnt << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << fd << ", " << printIovec(iov, iovcnt, 1024) << ", " << iovcnt << ")");
        ssize_t ret = 0;
        auto &preload = ld_preload::get_instance();
        ld_preload::lfi_socket* lfi_sock = nullptr;
        {
            std::unique_lock lock(preload.m_mutex);
            auto it_socket = preload.socket_ids.find(fd);
            if (it_socket != preload.socket_ids.end()) {
                lfi_sock = (*it_socket).second.get();
            }
        }
        if (!lfi_sock)
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
            ret = PROXY(writev)(fd, iov, iovcnt);
        }
        else
        {
            ret = preload.internal_sendmsg(*lfi_sock, iov, iovcnt);
        }
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << printIovec(iov, iovcnt) << ", " << iovcnt << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    ssize_t read(int fd, void *buf, size_t count)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(read)(fd, buf, count);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << fd << ", " << buf << ", " << count << ")");
        auto ret = PROXY(read)(fd, buf, count);
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << buf << ", " << count << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << fd << ", " << buf << ", " << count << ")");
        ssize_t ret = 0;
        auto &preload = ld_preload::get_instance();
        ld_preload::lfi_socket* lfi_sock = nullptr;
        {
            std::unique_lock lock(preload.m_mutex);
            auto it_socket = preload.socket_ids.find(fd);
            if (it_socket != preload.socket_ids.end()) {
                lfi_sock = (*it_socket).second.get();
            }
        }
        if (!lfi_sock)
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
            ret = PROXY(read)(fd, buf, count);
        }
        else
        {
            iovec iov = {.iov_base = buf, .iov_len = count};
            int iovcnt = 1;
            ret = preload.internal_recvmsg(*lfi_sock, &iov, iovcnt);
        }
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << buf << ", " << count << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(readv)(fd, iov, iovcnt);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << fd << ", " << printIovec(iov, iovcnt) << ", " << iovcnt << ")");
        auto ret = PROXY(readv)(fd, iov, iovcnt);
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << printIovec(iov, iovcnt, 1024) << ", " << iovcnt << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << fd << ", " << printIovec(iov, iovcnt) << ", " << iovcnt << ")");
        ssize_t ret = 0;
        auto &preload = ld_preload::get_instance();
        ld_preload::lfi_socket* lfi_sock = nullptr;
        {
            std::unique_lock lock(preload.m_mutex);
            auto it_socket = preload.socket_ids.find(fd);
            if (it_socket != preload.socket_ids.end()) {
                lfi_sock = (*it_socket).second.get();
            }
        }
        if (!lfi_sock)
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
            ret = PROXY(readv)(fd, iov, iovcnt);
        }
        else
        {
            ret = preload.internal_recvmsg(*lfi_sock, iov, iovcnt);
        }
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << printIovec(iov, iovcnt, 1024) << ", " << iovcnt << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) throw()
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(bind)(sockfd, addr, addrlen);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(bind)(sockfd, addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(bind)(sockfd, addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    int listen(int sockfd, int backlog) throw()
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(listen)(sockfd, backlog);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << backlog << ")");
        auto ret = PROXY(listen)(sockfd, backlog);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << backlog << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << backlog << ")");
        auto ret = PROXY(listen)(sockfd, backlog);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << backlog << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    ssize_t recv(int sockfd, void *buf, size_t len, int flags)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(recv)(sockfd, buf, len, flags);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << buf << ", " << len << ", " << flags << ")");
        // print_backtrace();
        // int * v = NULL;
        // v[0] = 1234;
        auto ret = PROXY(recv)(sockfd, buf, len, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << buf << ", " << len << ", " << flags << ")");
        ssize_t ret = 0;
        auto &preload = ld_preload::get_instance();
        ld_preload::lfi_socket* lfi_sock = nullptr;
        {
            std::unique_lock lock(preload.m_mutex);
            auto it_socket = preload.socket_ids.find(sockfd);
            if (it_socket != preload.socket_ids.end()) {
                lfi_sock = (*it_socket).second.get();
            }
        }
        if (!lfi_sock)
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
            ret = PROXY(recv)(sockfd, buf, len, flags);
        }
        else
        {
            iovec iov = {.iov_base = buf, .iov_len = len};
            int iovcnt = 1;
            ret = preload.internal_recvmsg(*lfi_sock, &iov, iovcnt);
        }
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    ssize_t recvfrom(int sockfd, void *__restrict buf, size_t len, int flags, struct sockaddr *__restrict src_addr, socklen_t *__restrict addrlen)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(recvfrom)(sockfd, buf, len, flags, src_addr, addrlen);
        }
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << src_addr << ", " << addrlen << ")");
        auto ret = PROXY(recvfrom)(sockfd, buf, len, flags, src_addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << src_addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
    }

    ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(recvmsg)(sockfd, msg, flags);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << msghdr_to_str(msg) << ", " << getMSGFlags(flags) << ")");
        auto ret = PROXY(recvmsg)(sockfd, msg, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msghdr_to_str(msg, std::min(msghdr_MIN, (ret<0?0:ret))) << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << msghdr_to_str(msg, 0) << ", " << getMSGFlags(flags) << ")");
        ssize_t ret = 0;
        auto &preload = ld_preload::get_instance();
        ld_preload::lfi_socket* lfi_sock = nullptr;
        {
            std::unique_lock lock(preload.m_mutex);
            auto it_socket = preload.socket_ids.find(sockfd);
            if (it_socket != preload.socket_ids.end()) {
                lfi_sock = (*it_socket).second.get();
            }
        }
        if (!lfi_sock)
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
            ret = PROXY(recvmsg)(sockfd, msg, flags);
        }
        else
        {
            ret = preload.internal_recvmsg(*lfi_sock, msg->msg_iov, msg->msg_iovlen);
        }
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msghdr_to_str(msg, std::min(msghdr_MIN, (ret<0?0:ret))) << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    ssize_t send(int sockfd, const void *buf, size_t len, int flags)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(send)(sockfd, buf, len, flags);
        }
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << buf << ", " << len << ", " << flags << ")");
        ssize_t ret = 0;
        auto &preload = ld_preload::get_instance();
        ld_preload::lfi_socket* lfi_sock = nullptr;
        {
            std::unique_lock lock(preload.m_mutex);
            auto it_socket = preload.socket_ids.find(sockfd);
            if (it_socket != preload.socket_ids.end()) {
                lfi_sock = (*it_socket).second.get();
            }
        }
        if (!lfi_sock)
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
            ret = PROXY(send)(sockfd, buf, len, flags);
        }
        else
        {
            iovec iov = {.iov_base = const_cast<void *>(buf), .iov_len = len};
            int iovcnt = 1;
            ret = preload.internal_sendmsg(*lfi_sock, &iov, iovcnt);
        }
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
    }

    ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(sendto)(sockfd, buf, len, flags, dest_addr, addrlen);
        }
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << dest_addr << ", " << addrlen << ")");
        auto ret = PROXY(sendto)(sockfd, buf, len, flags, dest_addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << dest_addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
    }

    ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(sendmsg)(sockfd, msg, flags);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << msghdr_to_str(msg, msghdr_MIN) << ", " << getMSGFlags(flags) << ")");
        auto ret = PROXY(sendmsg)(sockfd, msg, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msghdr_to_str(msg) << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << msghdr_to_str(msg, 1024) << ", " << getMSGFlags(flags) << ")");
        ssize_t ret = 0;
        auto &preload = ld_preload::get_instance();
        ld_preload::lfi_socket* lfi_sock = nullptr;
        {
            std::unique_lock lock(preload.m_mutex);
            auto it_socket = preload.socket_ids.find(sockfd);
            if (it_socket != preload.socket_ids.end()) {
                lfi_sock = (*it_socket).second.get();
            }
        }
        if (!lfi_sock)
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
            ret = PROXY(sendmsg)(sockfd, msg, flags);
        }
        else
        {
            ret = preload.internal_sendmsg(*lfi_sock, msg->msg_iov, msg->msg_iovlen);
        }
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msghdr_to_str(msg) << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    int shutdown(int sockfd, int how) throw()
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(shutdown)(sockfd, how);
        }
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << getShutdownHow(how) << ")");
        auto ret = PROXY(shutdown)(sockfd, how);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << getShutdownHow(how) << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
    }

    int getpeername(int sockfd, struct sockaddr *__restrict addr, socklen_t *__restrict addrlen) throw()
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(getpeername)(sockfd, addr, addrlen);
        }
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(getpeername)(sockfd, addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
    }

    int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *__restrict optlen) throw()
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(getsockopt)(sockfd, level, optname, optval, optlen);
        }
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ")");
        auto ret = PROXY(getsockopt)(sockfd, level, optname, optval, optlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
    }

    int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) throw()
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(setsockopt)(sockfd, level, optname, optval, optlen);
        }
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ")");
        auto ret = PROXY(setsockopt)(sockfd, level, optname, optval, optlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
    }

    int epoll_create(int size)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(epoll_create)(size);
        }
        debug("[LFI LD_PRELOAD] Start (" << size << ")");
        auto ret = PROXY(epoll_create)(size);
        debug("[LFI LD_PRELOAD] End (" << size << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
    }

    int epoll_create1(int flags)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(epoll_create1)(flags);
        }
        debug("[LFI LD_PRELOAD] Start (" << getAcceptFlags(flags) << ")");
        auto ret = PROXY(epoll_create1)(flags);
        debug("[LFI LD_PRELOAD] End (" << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
    }

    int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(epoll_ctl)(epfd, op, fd, event);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << epfd << ", " << getEPollop(op) << ", " << fd << ", " << getEPollEventStr(event, 1) << ")");
        auto ret = PROXY(epoll_ctl)(epfd, op, fd, event);
        debug("[LFI LD_PRELOAD] End (" << epfd << ", " << getEPollop(op) << ", " << fd << ", " << getEPollEventStr(event, 1) << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << epfd << ", " << getEPollop(op) << ", " << fd << ", " << getEPollEventStr(event, 1) << ")");
        int ret = 0;
        // ret = PROXY(epoll_ctl)(epfd, op, fd, event);
        // if (ret < 0) {
        //     if (errno == EEXIST) {
        //         ret = PROXY(epoll_ctl)(epfd, EPOLL_CTL_DEL, fd, event);
        //         if (ret < 0) {
        //             debug("[LFI LD_PRELOAD] End (" << epfd << ", " << getEPollop(op) << ", " << fd << ", " << getEPollEventStr(event, 1) << ") = " << ret << " " << STR_ERRNO(ret));
        //             return ret;
        //         }

        //         ret = PROXY(epoll_ctl)(epfd, op, fd, event);
        //         if (ret < 0) {
        //             debug("[LFI LD_PRELOAD] End (" << epfd << ", " << getEPollop(op) << ", " << fd << ", " << getEPollEventStr(event, 1) << ") = " << ret << " " << STR_ERRNO(ret));
        //             return ret;
        //         }
        //     } else {
        //         debug("[LFI LD_PRELOAD] End (" << epfd << ", " << getEPollop(op) << ", " << fd << ", " << getEPollEventStr(event, 1) << ") = " << ret << " " << STR_ERRNO(ret));
        //         return ret;
        //     }
        // }       

        auto &preload = ld_preload::get_instance();
        ld_preload::lfi_socket* lfi_sock = nullptr;
        {
            std::unique_lock lock(preload.m_mutex);
            auto it_socket = preload.socket_ids.find(fd);
            if (it_socket != preload.socket_ids.end()) {
                lfi_sock = (*it_socket).second.get();
            }
        }
        if (lfi_sock && lfi_sock->eventfd != -1) {
            debug("[LFI LD_PRELOAD] For fd " << fd << " have in socket_ids {" << lfi_sock->lfi_id << " " << lfi_sock->eventfd << "}");
            ret = PROXY(epoll_ctl)(epfd, op, lfi_sock->eventfd, event);
            debug("[LFI LD_PRELOAD] Adding epoll_ctl for eventfd " << lfi_sock->eventfd);
            if (ret < 0) {
                debug("[LFI LD_PRELOAD] End (" << epfd << ", " << getEPollop(op) << ", " << fd << ", " << getEPollEventStr(event, 1) << ") = " << ret << " " << STR_ERRNO(ret));
                return ret;
            }
        }else {
            ret = PROXY(epoll_ctl)(epfd, op, fd, event);
        }
        debug("[LFI LD_PRELOAD] End (" << epfd << ", " << getEPollop(op) << ", " << fd << ", " << getEPollEventStr(event, 1) << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
#endif
    }

    int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(epoll_wait)(epfd, events, maxevents, timeout);
        }
        debug("[LFI LD_PRELOAD] Start (" << epfd << ", " << maxevents << ", " << timeout << ")");
        auto ret = PROXY(epoll_wait)(epfd, events, maxevents, timeout);
        debug("[LFI LD_PRELOAD] End (" << epfd << ", " << (ret > 0 ? getEPollEventStr(events, ret) : "") << ", " << maxevents << ", " << timeout << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
    }

    int poll(struct pollfd *fds, nfds_t nfds, int timeout)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(poll)(fds, nfds, timeout);
        }
        debug("[LFI LD_PRELOAD] Start (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << timeout << ")");
        // Replace our fds
        auto &preload = ld_preload::get_instance();
        struct replace_fd {
            size_t index;
            int original_fd;
        };
        std::vector<replace_fd> replaced_fds;
        std::unique_lock lock(preload.m_mutex);
        for (size_t i = 0; i < nfds; i++)
        {
            auto it_socket = preload.socket_ids.find(fds[i].fd);
            if (it_socket != preload.socket_ids.end())
            {
                debug("[LFI LD_PRELOAD] find fd " << fds[i].fd << " in socket_ids");
                if (it_socket->second->eventfd != -1){
                    replaced_fds.emplace_back(replace_fd{.index = i, .original_fd = fds[i].fd});
                    fds[i].fd = it_socket->second->eventfd;
                }
            }
        }
        auto ret = PROXY(poll)(fds, nfds, timeout);
        if (ret < 0){
            return ret;
        }
        for (auto &repl : replaced_fds)
        {
            fds[repl.index].fd = repl.original_fd;
        }

        debug("[LFI LD_PRELOAD] End (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << timeout << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
    }

    int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const sigset_t *sigmask)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(ppoll)(fds, nfds, tmo_p, sigmask);
        }
        debug("[LFI LD_PRELOAD] Start (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << tmo_p << ", " << sigmask << ")");
        auto ret = PROXY(ppoll)(fds, nfds, tmo_p, sigmask);
        debug("[LFI LD_PRELOAD] End (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << tmo_p << ", " << sigmask << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
    }

    int select(int nfds, fd_set *__restrict readfds, fd_set *__restrict writefds, fd_set *__restrict exceptfds, struct timeval *__restrict timeout)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(select)(nfds, readfds, writefds, exceptfds, timeout);
        }
        debug("[LFI LD_PRELOAD] Start (" << nfds << ", " << readfds << ", " << writefds << ", " << exceptfds << ", " << timeout << ")");
        auto ret = PROXY(select)(nfds, readfds, writefds, exceptfds, timeout);
        debug("[LFI LD_PRELOAD] End (" << nfds << ", " << readfds << ", " << writefds << ", " << exceptfds << ", " << timeout << ") = " << ret << " " << STR_ERRNO(ret));
        return ret;
    }

#ifdef __cplusplus
}
#endif