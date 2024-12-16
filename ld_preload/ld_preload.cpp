
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

#include "ld_preload.hpp"

#include <poll.h>
#include <execinfo.h>
#include <sys/eventfd.h>

#include "proxy.hpp"
#include "debug.hpp"

#include "impl/socket.hpp"
#include "impl/fabric.hpp"
#include "impl/env.hpp"
#include "impl/ns.hpp"

bool ld_preload::is_caller_libfabric()
{
    void *buffer[100];
    int nptrs = backtrace(buffer, 100);

    char **symbols = backtrace_symbols(buffer, nptrs);
    if (symbols == nullptr)
    {
        std::cerr << "Error al obtener los símbolos." << std::endl;
        exit(EXIT_FAILURE);
    }

    bool out = false;
    for (int i = 0; i < nptrs; i++)
    {
        if (std::string(symbols[i]).find("libfabric.so") != std::string::npos)
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
    std::unique_lock lock(ld_preload.m_thread_eventfd_mutex);
    debug_info("[LFI LD_PRELOAD] Start");
    uint64_t buff = 1;
    int ret = 0;
    while (ld_preload.m_thread_eventfd_is_running)
    {
        ld_preload.m_thread_eventfd_cv.wait_for(lock, std::chrono::milliseconds(16));
        if (!ld_preload.m_thread_eventfd_is_running)
        {
            break;
        }

        std::unique_lock queue_lock(ld_preload.m_eventfd_requests_mutex);
        for (auto &[eventfd, request] : ld_preload.m_eventfd_requests_recv)
        {
            ret = LFI::LFI::wait(request, 0);
            if (ret == LFI::LFI_SUCCESS)
            {
                debug_info("[LFI LD_PRELOAD] Recv msg");
                buff = 4;
                PROXY(write)(eventfd, &buff, sizeof(buff));

                // Repost
                LFI::LFI::async_recv(&ld_preload.aux_buff, sizeof(ld_preload.aux_buff), LFI_TAG_RECV_LD_PRELOAD, request);
            }
        }
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

    debug("[LFI LD_PRELOAD] save eventfd " << new_eventfd << " in lfi_ids");

    auto comm = LFI::LFI::get_comm(ids.lfi_id);
    if (comm == nullptr)
        return -1;
    auto tuple = m_eventfd_requests_recv.emplace(std::piecewise_construct, std::forward_as_tuple(new_eventfd), std::forward_as_tuple(*comm));
    auto &request = tuple.first->second;
    LFI::LFI::async_recv(&aux_buff, sizeof(aux_buff), LFI_TAG_RECV_LD_PRELOAD, request);
    
    debug("[LFI LD_PRELOAD] async_recv fot eventfd " << new_eventfd);
    return new_eventfd;
}

#ifdef __cplusplus
extern "C"
{
#endif
    int socket(int domain, int type, int protocol)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(socket)(domain, type, protocol);
        }
#endif
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << socket_str(domain, type, protocol) << ")");
        auto ret = PROXY(socket)(domain, type, protocol);
        debug("[LFI LD_PRELOAD] End (" << socket_str(domain, type, protocol) << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << socket_str(domain, type, protocol) << ")");
        auto ret = PROXY(socket)(domain, type, protocol);
        if (ld_preload::is_caller_libfabric())
        {
            return ret;
        }
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
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(connect)(sockfd, addr, addrlen);
        }
#endif
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(connect)(sockfd, addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(connect)(sockfd, addr, addrlen);
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
            ret1 = LFI::socket::send(sockfd, &buff, sizeof(buff));
        }while(ret1 < 0 && errno == EAGAIN);
        // print("send "<<ret1<<" "<<STR_ERRNO);
        int ret2 = 0;
        do{
            // print("do recv");
            ret2 = LFI::socket::recv(sockfd, &buff, sizeof(buff));
            // print("recv "<<ret2<<" "<<strerror(errno));
            // std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }while(ret2 < 0 && errno == EAGAIN);

        int client_socket = LFI::socket::client_init(sockaddr_to_str(addr), LFI::env::get_instance().LFI_port);
        if (client_socket < 0)
        {
            print_error("socket::client_init (" << sockaddr_to_str(addr) << ", " << LFI::env::get_instance().LFI_port << ")");
        }
        auto new_fd = LFI::LFI::init_client(client_socket);
        if (new_fd < 0)
        {
            return new_fd;
        }

        // TODO handle error in close
        LFI::socket::close(client_socket);
        preload.socket_ids[sockfd].lfi_id = new_fd;
        debug("[LFI LD_PRELOAD] save fd " << sockfd << " in lfi_ids");

        preload.create_eventfd(preload.socket_ids[sockfd]);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
#endif
    }

    int accept(int sockfd, struct sockaddr *__restrict addr, socklen_t *__restrict addrlen)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return accept4(sockfd, addr, addrlen, 0);
        }
#endif
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
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(accept4)(sockfd, addr, addrlen, flags);
        }
#endif
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ")");
        auto ret = PROXY(accept4)(sockfd, addr, addrlen, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ")");
        auto ret = PROXY(accept4)(sockfd, addr, addrlen, flags);
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

        int server_socket = LFI::socket::server_init(LFI::ns::get_host_ip(), LFI::env::get_instance().LFI_port);
        if (server_socket < 0){
            return server_socket;
        }

        // print("sockfd "<<ret<<" "<<STR_ERRNO);
        size_t buff = 123;
        int ret2 = 0;
        do{
            // print("do recv");
            ret2 = LFI::socket::recv(ret, &buff, sizeof(buff));
            // print("recv "<<ret2<<" "<<strerror(errno));
            // std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }while(ret2 < 0 && errno == EAGAIN);
        int ret1 = 0;
        do{
            ret1 = LFI::socket::send(ret, &buff, sizeof(buff));
        }while(ret1 < 0 && errno == EAGAIN);
        // print("send "<<ret1<<" "<<STR_ERRNO);

        int client_socket = LFI::socket::accept(server_socket);
        if (client_socket < 0)
        {
            print_error("socket::client_init (" << server_socket << ")");
        }
        auto new_fd = LFI::LFI::init_server(client_socket);

        // TODO handle error in close
        LFI::socket::close(client_socket);
        LFI::socket::close(server_socket);
        if (new_fd < 0)
        {
            debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO);
            return new_fd;
        }

        preload.socket_ids.emplace(std::piecewise_construct, std::forward_as_tuple(ret), std::forward_as_tuple(ld_preload::lfi_socket{new_fd, -1}));
        preload.create_eventfd(preload.socket_ids[ret]);
        debug("[LFI LD_PRELOAD] save new lfi_socket fd " << ret << " lfi " << new_fd);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
#endif
    }

    int close(int fd)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(close)(fd);
        }
#endif
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
            if (it_socket->second.lfi_id != -1)
                ret = LFI::LFI::close_comm(it_socket->second.lfi_id);
            if (it_socket->second.eventfd != -1)
                ret = PROXY(close)(it_socket->second.eventfd);
            preload.socket_ids.erase(it_socket);
        }
        debug("[LFI LD_PRELOAD] End (" << fd << ") = " << ret << " " << STR_ERRNO);
        return ret;
#endif
    }

    ssize_t write(int fd, const void *buf, size_t count)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(write)(fd, buf, count);
        }
#endif
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
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(read)(fd, buf, count);
        }
#endif
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
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(bind)(sockfd, addr, addrlen);
        }
#endif
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
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(listen)(sockfd, backlog);
        }
#endif
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
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(recv)(sockfd, buf, len, flags);
        }
#endif
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
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(recvfrom)(sockfd, buf, len, flags, src_addr, addrlen);
        }
#endif
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << src_addr << ", " << addrlen << ")");
        auto ret = PROXY(recvfrom)(sockfd, buf, len, flags, src_addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << src_addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(recvmsg)(sockfd, msg, flags);
        }
#endif
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << msg << ", " << getMSGFlags(flags) << ")");
        auto ret = PROXY(recvmsg)(sockfd, msg, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msg << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << msg << ", " << getMSGFlags(flags) << ")");
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
            auto comm_id = it_socket->second.lfi_id;
            if (comm_id == -1)
            {
                debug("[LFI LD_PRELOAD] comm_id is -1");
                return -1;
            }

            auto comm = LFI::LFI::get_comm(comm_id);
            if (comm == nullptr){
                debug("[LFI LD_PRELOAD] comm is nullptr");
                return -1;
            }

            auto eventfd = it_socket->second.eventfd;
            if (eventfd != -1)
            {
                ret = PROXY(read)(eventfd, &preload.aux_buff, sizeof(preload.aux_buff));
                if (ret < 0){
                    return ret;
                }
            }

            ret = 0;
            for (size_t i = 0; i < msg->msg_iovlen; i++)
            {
                auto& iov = msg->msg_iov[i];
                debug("[LFI LD_PRELOAD] Start recv size "<<iov.iov_len);
                auto msg = LFI::LFI::recv(comm_id, iov.iov_base, iov.iov_len, 0);
                if (msg.error < 0)
                {
                    return -1;
                }
                ret += msg.size;
            }
        }

        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msg << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
#endif
    }

    ssize_t send(int sockfd, const void *buf, size_t len, int flags)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(send)(sockfd, buf, len, flags);
        }
#endif
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << buf << ", " << len << ", " << flags << ")");
        auto ret = PROXY(send)(sockfd, buf, len, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(sendto)(sockfd, buf, len, flags, dest_addr, addrlen);
        }
#endif
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << dest_addr << ", " << addrlen << ")");
        auto ret = PROXY(sendto)(sockfd, buf, len, flags, dest_addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << dest_addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(sendmsg)(sockfd, msg, flags);
        }
#endif
#ifdef ONLY_DEBUG
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << msg << ", " << getMSGFlags(flags) << ")");
        auto ret = PROXY(sendmsg)(sockfd, msg, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msg << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
#else
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << msg << ", " << getMSGFlags(flags) << ")");
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
            if (comm_id == -1)
            {
                return -1;
            }

            auto comm = LFI::LFI::get_comm(comm_id);
            if (comm == nullptr)
                return -1;

            auto eventfd = it_socket->second.eventfd;
            if (eventfd != -1)
            {   
                debug("[LFI LD_PRELOAD] Send notification");
                LFI::LFI::send(comm_id, &preload.aux_buff, sizeof(preload.aux_buff), LFI_TAG_RECV_LD_PRELOAD);
                debug("[LFI LD_PRELOAD] Sended notification");
            }

            size_t buff_size = 0;
            for (size_t i = 0; i < msg->msg_iovlen; i++)
            {
                buff_size += msg->msg_iov[i].iov_len;
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
            auto msg = LFI::LFI::send(comm_id, buff.data(), buff.size(), 0);

            if (msg.error < 0){
                return msg.error;
            }

            ret = msg.size;
        }
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msg << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
#endif
    }

    int shutdown(int sockfd, int how)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(shutdown)(sockfd, how);
        }
#endif
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << getShutdownHow(how) << ")");
        auto ret = PROXY(shutdown)(sockfd, how);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << getShutdownHow(how) << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int getpeername(int sockfd, struct sockaddr *__restrict addr, socklen_t *__restrict addrlen)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(getpeername)(sockfd, addr, addrlen);
        }
#endif
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(getpeername)(sockfd, addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *__restrict optlen)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(getsockopt)(sockfd, level, optname, optval, optlen);
        }
#endif
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ")");
        auto ret = PROXY(getsockopt)(sockfd, level, optname, optval, optlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(setsockopt)(sockfd, level, optname, optval, optlen);
        }
#endif
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ")");
        auto ret = PROXY(setsockopt)(sockfd, level, optname, optval, optlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int epoll_create(int size)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(epoll_create)(size);
        }
#endif
        debug("[LFI LD_PRELOAD] Start (" << size << ")");
        auto ret = PROXY(epoll_create)(size);
        debug("[LFI LD_PRELOAD] End (" << size << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int epoll_create1(int flags)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(epoll_create1)(flags);
        }
#endif
        debug("[LFI LD_PRELOAD] Start (" << getAcceptFlags(flags) << ")");
        auto ret = PROXY(epoll_create1)(flags);
        debug("[LFI LD_PRELOAD] End (" << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(epoll_ctl)(epfd, op, fd, event);
        }
#endif
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
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(epoll_wait)(epfd, events, maxevents, timeout);
        }
#endif
        debug("[LFI LD_PRELOAD] Start (" << epfd << ", " << getEPollEventStr(events, maxevents) << ", " << maxevents << ", " << timeout << ")");
        auto ret = PROXY(epoll_wait)(epfd, events, maxevents, timeout);
        debug("[LFI LD_PRELOAD] End (" << epfd << ", " << getEPollEventStr(events, maxevents) << ", " << maxevents << ", " << timeout << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int poll(struct pollfd *fds, nfds_t nfds, int timeout)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(poll)(fds, nfds, timeout);
        }
#endif
        debug("[LFI LD_PRELOAD] Start (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << timeout << ")");
        auto ret = PROXY(poll)(fds, nfds, timeout);
        debug("[LFI LD_PRELOAD] End (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << timeout << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const sigset_t *sigmask)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(ppoll)(fds, nfds, tmo_p, sigmask);
        }
#endif
        debug("[LFI LD_PRELOAD] Start (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << tmo_p << ", " << sigmask << ")");
        auto ret = PROXY(ppoll)(fds, nfds, tmo_p, sigmask);
        debug("[LFI LD_PRELOAD] End (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << tmo_p << ", " << sigmask << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int select(int nfds, fd_set *__restrict readfds, fd_set *__restrict writefds, fd_set *__restrict exceptfds, struct timeval *__restrict timeout)
    {
#ifdef DEBUG
        if (ld_preload::is_caller_libfabric())
        {
            return PROXY(select)(nfds, readfds, writefds, exceptfds, timeout);
        }
#endif
        debug("[LFI LD_PRELOAD] Start (" << nfds << ", " << readfds << ", " << writefds << ", " << exceptfds << ", " << timeout << ")");
        auto ret = PROXY(select)(nfds, readfds, writefds, exceptfds, timeout);
        debug("[LFI LD_PRELOAD] End (" << nfds << ", " << readfds << ", " << writefds << ", " << exceptfds << ", " << timeout << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

#ifdef __cplusplus
}
#endif