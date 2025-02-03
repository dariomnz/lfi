
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
#include "ld_preload.hpp"

#include <poll.h>
#include <execinfo.h>
#include <sys/eventfd.h>

#include "proxy.hpp"
#include "debug.hpp"

#include "impl/socket.hpp"
#include "impl/env.hpp"
#include "impl/ns.hpp"

using namespace LFI;

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
    for (int i = 0; i < nptrs; i++)
    {
        if (std::string_view(symbols[i]).find("libpsm2.so") != std::string_view::npos ||
        std::string_view(symbols[i]).find("libfabric.so") != std::string_view::npos)
        {
            out = true;
            break;
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
    m_thread_eventfd = std::thread(thread_eventfd_loop);
    debug("[LFI LD_PRELOAD] End");
    return;
}

void ld_preload::thread_eventfd_end()
{
    debug_info("[LFI LD_PRELOAD] Start");
    {
        std::lock_guard lock(m_thread_eventfd_mutex);
        if (!m_thread_eventfd_is_running)
            return;
        m_thread_eventfd_is_running = false;
    }
    m_thread_eventfd_cv.notify_one();
    m_thread_eventfd.join();

    debug_info("[LFI LD_PRELOAD] End");
    return;
}

void ld_preload::thread_eventfd_loop()
{
    auto &ld_preload = ld_preload::get_instance();
    auto &lfi = ld_preload.m_lfi;
    std::unique_lock lock(ld_preload.m_thread_eventfd_mutex);
    debug_info("[LFI LD_PRELOAD] Start");
    uint64_t buff = 1;
    uint64_t aux_buff_shm = 0;
    uint64_t aux_buff_peer = 0;
    std::unique_ptr<LFI::lfi_request> shm_request, peer_request;
    auto any_recv = [&](LFI::lfi_request& req, bool is_shm){
        LFI::lfi_msg msg;
        if (is_shm){
            msg = lfi->async_recv(&aux_buff_shm, sizeof(aux_buff_shm), LFI_TAG_RECV_LD_PRELOAD, req);
        }else{
            msg = lfi->async_recv(&aux_buff_peer, sizeof(aux_buff_peer), LFI_TAG_RECV_LD_PRELOAD, req);
        }
        if (msg.error < 0){
            print("Error in async_recv")
            return -1;
        }
        return 0;
    };
    while (ld_preload.m_thread_eventfd_is_running)
    {
        ld_preload.m_thread_eventfd_cv.wait_for(lock, std::chrono::milliseconds(16));
        if (!ld_preload.m_thread_eventfd_is_running)
        {
            break;
        }
        
        if (!shm_request){
            auto comm = lfi->get_comm(LFI::ANY_COMM_SHM);
            if (!comm){
                print("Error get_comm ANY_COMM_SHM");
                continue;
            }
            shm_request = std::make_unique<LFI::lfi_request>(comm);
            if (!shm_request){
                print("Error shm_request is null");
                continue;
            }
            
            if (any_recv(*shm_request, true)){
                print_error("shm any_recv");
            }
        }

        if (!peer_request){
            auto comm = lfi->get_comm(LFI::ANY_COMM_PEER);
            if (!comm){
                print("Error get_comm ANY_COMM_PEER");
                continue;
            }
            peer_request = std::make_unique<LFI::lfi_request>(comm);
            if (!peer_request){
                print("Error peer_request is null");
                continue;
            }

            if (any_recv(*peer_request, false)){
                print_error("peer any_recv");
            }
        }

        std::vector<std::reference_wrapper<LFI::lfi_request>> requests = {*shm_request, *peer_request};
        int completed = lfi->wait_num(requests, 1, 500);
        int source = -1;
        if (completed == 0) {
            // SHM
            source = ((shm_request->entry.tag & 0x0000'00FF'FFFF'0000) >> 16);
            buff = aux_buff_shm;
            // Reuse the request
            if (any_recv(*shm_request, true)){
                print_error("peer any_recv");
            }
        } else if (completed == 1) {
            // PEER
            source = ((peer_request->entry.tag & 0x0000'00FF'FFFF'0000) >> 16);
            buff = aux_buff_peer;
            // Reuse the request
            if (any_recv(*peer_request, false)){
                print_error("peer any_recv");
            }
        } else if (completed == -LFI::LFI_TIMEOUT) {
            continue;
        } else {
            print_error("lfi wait_num");
            continue;
        }

        int socket = -1;
        {
            std::unique_lock queue_lock(ld_preload.m_map_comm_socket_mutex);
            auto it = ld_preload.m_map_comm_socket.find(source);
            if (it == ld_preload.m_map_comm_socket.end()){
                print_error("Error find the comm "<<source);
                continue;
            }
            socket = it->second;
        }

            
        std::unique_lock queue_lock(ld_preload.m_mutex);
        auto it = ld_preload.socket_ids.find(socket);
        if (it == ld_preload.socket_ids.end()){
            print_error("Error find the socket "<<socket);
            continue;
        }

        auto& lfi_socket = it->second;

        debug_info("[LFI LD_PRELOAD] Recv notification of msg of size "<<buff);
        std::unique_lock eventfd_lock(lfi_socket.m_mutex);
        lfi_socket::buffered new_buffer;
        new_buffer.buffer.resize(buff, 0);
        auto msg = lfi->recv(lfi_socket.lfi_id, new_buffer.buffer.data(), new_buffer.buffer.size(), LFI_TAG_BUFFERED_LD_PRELOAD);
        if (msg.error < 0){
            print_error("Error recv buffered in comm "<<lfi_socket.lfi_id<<" error "<<msg.error);
            continue;
        }
        debug_info("[LFI LD_PRELOAD] Recv of msg of size "<<msg.size);
        // debug_info("[LFI LD_PRELOAD] Recv of msg "<<to_hex(new_buffer.buffer));

        lfi_socket.buffers.emplace_back(std::move(new_buffer));
        debug_info("[LFI LD_PRELOAD] Actual msg in queue "<<lfi_socket.buffers.size());
        buff=1;
        auto ret = PROXY(write)(lfi_socket.eventfd, &buff, sizeof(buff));
        if (ret < 0){
            debug("[LFI LD_PRELOAD] Error writing eventfd "<<lfi_socket.eventfd<<" error "<<ret<<" "<<strerror(errno));
            continue;
        }
        
        debug_info("[LFI LD_PRELOAD] msg queue "<<lfi_socket);
    }
    debug_info("[LFI LD_PRELOAD] End");
}

int ld_preload::create_eventfd(lfi_socket &ids)
{
    auto new_eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (new_eventfd < 0)
    {
        return new_eventfd;
    }
    ids.eventfd = new_eventfd;
    
    uint64_t buff=1;
    auto ret = PROXY(write)(new_eventfd, &buff, sizeof(buff));  
    if (ret < 0){
        debug("[LFI LD_PRELOAD] Error writing eventfd "<<new_eventfd<<" error "<<ret<<" "<<strerror(errno));
    }

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

#ifdef __cplusplus
extern "C"
{
#endif
    int socket(int domain, int type, int protocol)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(socket)(domain, type, protocol);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << socket_str(domain, type, protocol) << ")");
        auto ret = PROXY(socket)(domain, type, protocol);
        debug("[LFI LD_PRELOAD] End (" << socket_str(domain, type, protocol) << ") = " << ret << " " << STR_ERRNO);
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
            preload.socket_ids.emplace(std::piecewise_construct, std::forward_as_tuple(ret), std::forward_as_tuple());
            debug("[LFI LD_PRELOAD] save fd " << ret << " in socket_ids");
        }
        debug("[LFI LD_PRELOAD] End (" << socket_str(domain, type, protocol) << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(connect)(sockfd, addr, addrlen);
        auto save_errno = errno;
        if (ret == -1 && errno != EINPROGRESS){
            return ret;
        }

        auto &preload = ld_preload::get_instance();
        
        decltype(preload.socket_ids.find(sockfd)) it_socket;
        {
            std::unique_lock lock(preload.m_mutex);
            it_socket = preload.socket_ids.find(sockfd);
        }
        if (it_socket == preload.socket_ids.end())
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
            return ret;
        }
        // std::this_thread::sleep_for(std::chrono::milliseconds(500));
        // print("sockfd "<<sockfd<<" "<<STR_ERRNO);
        size_t buff = 123;
        int ret1 = 0;
        do{
            ret1 = socket::send(sockfd, &buff, sizeof(buff));
        }while(ret1 < 0 && errno == EAGAIN);
        // print("send "<<ret1<<" "<<STR_ERRNO);
        int ret2 = 0;
        do{
            // print("do recv");
            ret2 = socket::recv(sockfd, &buff, sizeof(buff));
            // print("recv "<<ret2<<" "<<strerror(errno));
            // std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }while(ret2 < 0 && errno == EAGAIN);

        std::string server_addr = sockaddr_to_str(addr);
        debug_info("Try connect to " << server_addr);

        int client_socket = socket::client_init(server_addr, LFI::env::get_instance().LFI_port);
        if (client_socket < 0)
        {
            print_error("socket::client_init (" << server_addr << ", " << LFI::env::get_instance().LFI_port << ")");
        }
        auto new_fd = preload.m_lfi->init_client(client_socket);
        if (new_fd < 0)
        {
            return new_fd;
        }

        // TODO handle error in close
        socket::close(client_socket);
        preload.socket_ids[sockfd].lfi_id = new_fd;
        debug("[LFI LD_PRELOAD] save fd " << sockfd << " in lfi_ids");
        {
            std::unique_lock lock(preload.m_map_comm_socket_mutex);
            preload.m_map_comm_socket.emplace(std::piecewise_construct, std::forward_as_tuple(new_fd), std::forward_as_tuple(sockfd));
        }
        preload.create_eventfd(preload.socket_ids[sockfd]);
        errno = save_errno;
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = accept4(sockfd, addr, addrlen, 0);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ")");
        auto ret = PROXY(accept4)(sockfd, addr, addrlen, flags);
        debug("[LFI LD_PRELOAD] Accept4 ret = "<<ret);
        if (ret < 0) return ret;

        auto &preload = ld_preload::get_instance();
        decltype(preload.socket_ids.find(sockfd)) it_socket;
        {
            std::unique_lock lock(preload.m_mutex);
            it_socket = preload.socket_ids.find(sockfd);
        }
        if (it_socket == preload.socket_ids.end())
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
            return ret;
        }

        int server_socket = socket::server_init(LFI::ns::get_host_ip(), LFI::env::get_instance().LFI_port);
        if (server_socket < 0){
            return server_socket;
        }

        // print("sockfd "<<ret<<" "<<STR_ERRNO);
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
        // print("send "<<ret1<<" "<<STR_ERRNO);

        int client_socket = socket::accept(server_socket);
        if (client_socket < 0)
        {
            print_error("socket::client_init (" << server_socket << ")");
        }
        
        auto new_fd = preload.m_lfi->init_server(client_socket);

        // TODO handle error in close
        socket::close(client_socket);
        socket::close(server_socket);
        if (new_fd < 0)
        {
            debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO);
            return new_fd;
        }

        {
            std::unique_lock lock(preload.m_mutex);
            auto it = preload.socket_ids.emplace(std::piecewise_construct, std::forward_as_tuple(ret), std::forward_as_tuple());
            auto& buff = it.first->second;
            buff.lfi_id = new_fd;
            preload.create_eventfd(preload.socket_ids[ret]);
        }
        {
            std::unique_lock lock(preload.m_map_comm_socket_mutex);
            preload.m_map_comm_socket.emplace(std::piecewise_construct, std::forward_as_tuple(new_fd), std::forward_as_tuple(ret));
        }
        debug("[LFI LD_PRELOAD] save new lfi_socket fd " << ret << " lfi " << new_fd);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << fd << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << fd << ")");
        int ret = PROXY(close)(fd);

        auto &preload = ld_preload::get_instance();
        decltype(preload.socket_ids.find(fd)) it_socket;
        {
            std::unique_lock lock(preload.m_mutex);
            it_socket = preload.socket_ids.find(fd);
        }
        if (it_socket == preload.socket_ids.end())
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
        }
        else
        {
            if (it_socket->second.lfi_id != -1){
                ret = preload.m_lfi->close_comm(it_socket->second.lfi_id);
            }
            {
                std::unique_lock lock(preload.m_map_comm_socket_mutex);
                preload.m_map_comm_socket.erase(it_socket->second.lfi_id);
            }
            preload.destroy_eventfd(it_socket->second);
            {
                std::unique_lock lock(preload.m_mutex);
                preload.socket_ids.erase(it_socket);
            }
        }
        debug("[LFI LD_PRELOAD] End (" << fd << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << buf << ", " << count << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << fd << ", " << buf << ", " << count << ")");
        auto ret = PROXY(write)(fd, buf, count);
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << buf << ", " << count << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << buf << ", " << count << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << fd << ", " << buf << ", " << count << ")");
        auto ret = PROXY(read)(fd, buf, count);
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << buf << ", " << count << ") = " << ret << " " << STR_ERRNO);
        return ret;
#endif
    }

    int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(bind)(sockfd, addr, addrlen);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(bind)(sockfd, addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(bind)(sockfd, addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
#endif
    }

    int listen(int sockfd, int backlog)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(listen)(sockfd, backlog);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << backlog << ")");
        auto ret = PROXY(listen)(sockfd, backlog);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << backlog << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << backlog << ")");
        auto ret = PROXY(listen)(sockfd, backlog);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << backlog << ") = " << ret << " " << STR_ERRNO);
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
        auto ret = PROXY(recv)(sockfd, buf, len, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << buf << ", " << len << ", " << flags << ")");
        auto ret = PROXY(recv)(sockfd, buf, len, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << src_addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msghdr_to_str(msg, std::min(1024l, ret)) << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << msghdr_to_str(msg) << ", " << getMSGFlags(flags) << ")");
        ssize_t ret = 0;
        auto &preload = ld_preload::get_instance();
        decltype(preload.socket_ids.find(sockfd)) it_socket;
        {
            std::unique_lock lock(preload.m_mutex);
            it_socket = preload.socket_ids.find(sockfd);
        }
        if (it_socket == preload.socket_ids.end())
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
            ret = PROXY(recvmsg)(sockfd, msg, flags);
        }
        else
        {
            debug("[LFI LD_PRELOAD] found find fd in socket_ids");
            auto comm_id = it_socket->second.lfi_id;
            if (comm_id == -1)
            {
                debug("[LFI LD_PRELOAD] comm_id is -1");
                return -1;
            }

            auto comm = preload.m_lfi->get_comm(comm_id);
            if (comm == nullptr){
                debug("[LFI LD_PRELOAD] comm is nullptr");
                return -1;
            }

            auto& lfi_socket = it_socket->second;

            ret = 0;
            std::unique_lock lock(lfi_socket.m_mutex);
            if (lfi_socket.buffers.size() == 0){
                debug("[LFI LD_PRELOAD] msg_size empty");  
                errno = EAGAIN;
                return -1;
            }
            auto& buff = lfi_socket.buffers.front();
            for (size_t i = 0; i < msg->msg_iovlen; i++)
            {
                if (buff.buffer.size() == buff.consumed){
                    break;
                }
                auto& iov = msg->msg_iov[i];
                debug("[LFI LD_PRELOAD] start msg queue "<<lfi_socket);

                auto to_copy = std::min(iov.iov_len, (buff.buffer.size()-buff.consumed));

                std::memcpy(iov.iov_base, (buff.buffer.data()+buff.consumed), to_copy);

                ret += to_copy;
                buff.consumed += to_copy;
                debug("[LFI LD_PRELOAD] end msg queue "<<lfi_socket);

                // if (buff == 0) {
                //     debug("[LFI LD_PRELOAD] Finish");
                //     break;
                // } 

                // uint64_t len = iov.iov_len;

                // if ()
                
                // // TODO: revise the min size to start sending
                // if (buff > 1024){
                //     debug("[LFI LD_PRELOAD] Start send buffer size "<<len);
                //     auto msg_send = preload.m_lfi->send(comm_id, &len, sizeof(len), LFI_TAG_BUFF_SIZE_LD_PRELOAD);
                //     if (msg_send.error < 0)
                //     {
                //         debug("[LFI LD_PRELOAD] Error in recv");
                //         return -1;
                //     }
                //     debug("[LFI LD_PRELOAD] Start sended buffer size "<<len);
                // }

                // debug("[LFI LD_PRELOAD] Start recv size "<<iov.iov_len);
                // auto msg_recv = preload.m_lfi->recv(comm_id, iov.iov_base, iov.iov_len, 0);
                // if (msg_recv.error < 0)
                // {
                //     debug("[LFI LD_PRELOAD] Error in recv");
                //     return -1;
                // }
                // ret += msg_recv.size;
                // debug("[LFI LD_PRELOAD] End recv size "<<msg_recv.size);
                    
                // // std::unique_lock lock(preload.m_eventfd_mutex);
                // if (preload.msg_size < msg_recv.size){
                //     preload.msg_size = 0;
                // }else{
                //     preload.msg_size-=msg_recv.size;
                // }
                // debug("[LFI LD_PRELOAD] Actual msg_size "<<preload.msg_size);
            }
            
            if (buff.buffer.size() == buff.consumed){
                lfi_socket.buffers.pop_front();
            }
            debug("[LFI LD_PRELOAD] msg queue "<<lfi_socket);
            // std::unique_lock lock(preload.m_eventfd_mutex);
            // if (preload.msg_size > 0){
            //     debug("[LFI LD_PRELOAD] write to eventfd a buff of "<<preload.msg_size);
            //     // ssize_t ret_write = PROXY(write)(eventfd, &preload.msg_size, sizeof(preload.msg_size));
            //     // if (ret_write < 0){
            //     //     debug("[LFI LD_PRELOAD] Error write in eventfd "<<eventfd<<" err "<<ret_write<<" "<<strerror(errno));  
            //     //     return ret_write;
            //     // }
            // }else{
            //     debug("[LFI LD_PRELOAD] not write to eventfd msg_size empty");
            // }
        }
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msghdr_to_str(msg, std::min(1024l, ret)) << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO);
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
        auto ret = PROXY(send)(sockfd, buf, len, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << dest_addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(sendmsg)(sockfd, msg, flags);
        }
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << msghdr_to_str(msg, 1024) << ", " << getMSGFlags(flags) << ")");
        auto ret = PROXY(sendmsg)(sockfd, msg, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msghdr_to_str(msg) << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << msghdr_to_str(msg, 1024) << ", " << getMSGFlags(flags) << ")");
        ssize_t ret = 0;
        auto &preload = ld_preload::get_instance();
        decltype(preload.socket_ids.find(sockfd)) it_socket;
        {
            std::unique_lock lock(preload.m_mutex);
            it_socket = preload.socket_ids.find(sockfd);
        }
        if (it_socket == preload.socket_ids.end())
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
            ret = PROXY(sendmsg)(sockfd, msg, flags);
        }
        else
        {
            auto comm_id = it_socket->second.lfi_id;
            if (comm_id == -1){
                debug("[LFI LD_PRELOAD] Error comm_id -1");
                return -1;
            }

            auto comm = preload.m_lfi->get_comm(comm_id);
            if (comm == nullptr){
                debug("[LFI LD_PRELOAD] Error comm not found");
                return -1;
            }

            uint64_t buff_size = 0;
            for (size_t i = 0; i < msg->msg_iovlen; i++)
            {
                buff_size += msg->msg_iov[i].iov_len;
            }

            auto eventfd = it_socket->second.eventfd;
            if (eventfd != -1){
                debug("[LFI LD_PRELOAD] Send notification of "<<buff_size);
                auto msg_not = preload.m_lfi->send(comm_id, &buff_size, sizeof(buff_size), LFI_TAG_RECV_LD_PRELOAD);
                if (msg_not.error < 0){
                    debug("[LFI LD_PRELOAD] Error in send notification "<<msg_not.error<<" "<<LFI::lfi_strerror(msg_not.error));
                    return msg_not.error;
                }
                debug("[LFI LD_PRELOAD] Sended notification of "<<buff_size);
            }

            debug("[LFI LD_PRELOAD] Buff size "<<buff_size);
            std::vector<uint8_t> buff;
            buff.reserve(buff_size);
            for (size_t i = 0; i < msg->msg_iovlen; ++i) {
                const struct iovec &iov = msg->msg_iov[i];
                const uint8_t *buffer = static_cast<const uint8_t *>(iov.iov_base);

                buff.insert(buff.end(), buffer, buffer + iov.iov_len);
            }

            
            debug("[LFI LD_PRELOAD] Start send size "<<buff.size());
            auto msg_send = preload.m_lfi->send(comm_id, buff.data(), buff.size(), LFI_TAG_BUFFERED_LD_PRELOAD);
            if (msg_send.error < 0){
                debug("[LFI LD_PRELOAD] Error in send "<<msg_send.error<<" "<<LFI::lfi_strerror(msg_send.error));
                return msg_send.error;
            }

            ret = msg_send.size;
            debug("[LFI LD_PRELOAD] End send size "<<ret);
            // uint64_t already_send = 0;
            // uint64_t to_send = buff.size();
            // while(to_send > 0){
            //     uint64_t len = 0;
            //     // TODO: revise the min size to start sending
            //     if (to_send > 1024){
            //         debug("[LFI LD_PRELOAD] Start recv size of buffer");
            //         auto msg_recv = preload.m_lfi->recv(comm_id, &len, sizeof(len), LFI_TAG_BUFF_SIZE_LD_PRELOAD);
            //         if (msg_recv.error < 0){
            //             debug("[LFI LD_PRELOAD] Error in recv "<<msg_recv.error<<" "<<LFI::lfi_strerror(msg_recv.error));
            //             return msg_recv.error;
            //         }

            //         debug("[LFI LD_PRELOAD] End recv size of buffer "<<len);
            //     }else{
            //         len = 1024;
            //         debug("[LFI LD_PRELOAD] Not recv size of buffer size less than 1024 : "<<to_send);
            //     }

            //     int64_t sending = std::min(len, to_send);
            //     debug("[LFI LD_PRELOAD] Start send size "<<sending);
            //     auto msg_send = preload.m_lfi->send(comm_id, buff.data()+already_send, sending, 0);
            //     if (msg_send.error < 0){
            //         debug("[LFI LD_PRELOAD] Error in send "<<msg_send.error<<" "<<LFI::lfi_strerror(msg_send.error));
            //         return msg_send.error;
            //     }

            //     already_send += msg_send.size;
            //     to_send -= msg_send.size;
            //     debug("[LFI LD_PRELOAD] Already send "<<already_send<<" to send "<<to_send);
            // }
            // ret = already_send;
        }
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msghdr_to_str(msg) << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
#endif
    }

    int shutdown(int sockfd, int how)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(shutdown)(sockfd, how);
        }
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << getShutdownHow(how) << ")");
        auto ret = PROXY(shutdown)(sockfd, how);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << getShutdownHow(how) << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int getpeername(int sockfd, struct sockaddr *__restrict addr, socklen_t *__restrict addrlen)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(getpeername)(sockfd, addr, addrlen);
        }
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(getpeername)(sockfd, addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *__restrict optlen)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(getsockopt)(sockfd, level, optname, optval, optlen);
        }
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ")");
        auto ret = PROXY(getsockopt)(sockfd, level, optname, optval, optlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(setsockopt)(sockfd, level, optname, optval, optlen);
        }
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ")");
        auto ret = PROXY(setsockopt)(sockfd, level, optname, optval, optlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << size << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << epfd << ", " << getEPollop(op) << ", " << fd << ", " << getEPollEventStr(event, 1) << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << epfd << ", " << getEPollop(op) << ", " << fd << ", " << getEPollEventStr(event, 1) << ")");
        auto ret = PROXY(epoll_ctl)(epfd, op, fd, event);
        if (ret < 0)
        {
            return ret;
        }

        auto &preload = ld_preload::get_instance();
        decltype(preload.socket_ids.find(fd)) it_socket;
        {
            std::unique_lock lock(preload.m_mutex);
            it_socket = preload.socket_ids.find(fd);
        }
        if (it_socket != preload.socket_ids.end())
        {
            debug("[LFI LD_PRELOAD] For fd " << fd << " have in socket_ids {" << it_socket->second.lfi_id << " " << it_socket->second.eventfd << "}");
            if (it_socket->second.eventfd != -1)
            {
                ret = PROXY(epoll_ctl)(epfd, op, it_socket->second.eventfd, event);
                debug("[LFI LD_PRELOAD] Adding epoll_ctl for eventfd " << it_socket->second.eventfd);
                if (ret < 0)
                {
                    return ret;
                }
            }
        }
        debug("[LFI LD_PRELOAD] End (" << epfd << ", " << getEPollop(op) << ", " << fd << ", " << getEPollEventStr(event, 1) << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << epfd << ", " << (ret > 0 ? getEPollEventStr(events, ret) : "") << ", " << maxevents << ", " << timeout << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int poll(struct pollfd *fds, nfds_t nfds, int timeout)
    {
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(poll)(fds, nfds, timeout);
        }
        debug("[LFI LD_PRELOAD] Start (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << timeout << ")");
        auto ret = PROXY(poll)(fds, nfds, timeout);
        debug("[LFI LD_PRELOAD] End (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << timeout << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << tmo_p << ", " << sigmask << ") = " << ret << " " << STR_ERRNO);
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
        debug("[LFI LD_PRELOAD] End (" << nfds << ", " << readfds << ", " << writefds << ", " << exceptfds << ", " << timeout << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

#ifdef __cplusplus
}
#endif