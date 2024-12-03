
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
 *  but WITHOUT ANY WARRANTY
 * {
 * } without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with LFI.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#define DEBUG
#include "ld_preload.hpp"
#include "proxy.hpp"
#include "debug.hpp"
#include <poll.h>

#ifdef __cplusplus
extern "C"
{
#endif

    int socket(int domain, int type, int protocol)
    {
        debug("[LFI LD_PRELOAD] Start (" << socket_str(domain, type, protocol) << ")");
        auto ret = PROXY(socket)(domain, type, protocol);
        if (ret < 0)
        {
            return ret;
        }
        auto &preload = ld_preload::get_instance();
        preload.socket_ids.emplace(ret);
        debug("[LFI LD_PRELOAD] save fd " << ret << " in socket_ids");
        debug("[LFI LD_PRELOAD] End (" << socket_str(domain, type, protocol) << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(connect)(sockfd, addr, addrlen);
        if (ret < 0)
        {
            return ret;
        }
        auto &preload = ld_preload::get_instance();
        preload.lfi_ids.emplace(sockfd);
        debug("[LFI LD_PRELOAD] save fd " << sockfd << " in lfi_ids");
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int accept(int sockfd, struct sockaddr *__restrict addr, socklen_t *__restrict addrlen)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = accept4(sockfd, addr, addrlen, 0);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int accept4(int sockfd, struct sockaddr *__restrict addr, socklen_t *__restrict addrlen, int flags)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ")");
        auto ret = PROXY(accept4)(sockfd, addr, addrlen, flags);
        if (ret < 0)
        {
            return ret;
        }
        auto &preload = ld_preload::get_instance();
        preload.lfi_ids.emplace(ret);
        debug("[LFI LD_PRELOAD] save fd " << ret << " in lfi_ids");
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ", " << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int close(int fd)
    {
        debug("[LFI LD_PRELOAD] Start (" << fd << ")");
        int ret = 0;
        auto &preload = ld_preload::get_instance();
        auto it_fabric = preload.lfi_ids.find(fd);
        if (it_fabric == preload.lfi_ids.end())
        {
            debug("[LFI LD_PRELOAD] cannot find fd in lfi_ids");
        }
        else
        {
            preload.lfi_ids.erase(fd);
            ret = PROXY(close)(fd);
        }
        auto it_socket = preload.socket_ids.find(fd);
        if (it_socket == preload.socket_ids.end())
        {
            debug("[LFI LD_PRELOAD] cannot find fd in socket_ids");
        }
        else
        {
            preload.socket_ids.erase(fd);
            ret = PROXY(close)(fd);
        }
        if (it_fabric == preload.lfi_ids.end() && it_socket == preload.socket_ids.end())
        {
            debug("[LFI LD_PRELOAD] cannot find fd in lfi_ids and socket_ids");
            return -1;
        }
        debug("[LFI LD_PRELOAD] End (" << fd << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    ssize_t write(int fd, const void *buf, size_t count)
    {
        debug("[LFI LD_PRELOAD] Start (" << fd << ", " << buf << ", " << count << ")");
        auto ret = PROXY(write)(fd, buf, count);
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << buf << ", " << count << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    ssize_t read(int fd, void *buf, size_t count)
    {
        debug("[LFI LD_PRELOAD] Start (" << fd << ", " << buf << ", " << count << ")");
        auto ret = PROXY(read)(fd, buf, count);
        debug("[LFI LD_PRELOAD] End (" << fd << ", " << buf << ", " << count << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(bind)(sockfd, addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int listen(int sockfd, int backlog)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << backlog << ")");
        auto ret = PROXY(listen)(sockfd, backlog);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << backlog << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    ssize_t recv(int sockfd, void *buf, size_t len, int flags)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << buf << ", " << len << ", " << flags << ")");
        auto ret = PROXY(recv)(sockfd, buf, len, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    ssize_t recvfrom(int sockfd, void *__restrict buf, size_t len, int flags, struct sockaddr *__restrict src_addr, socklen_t *__restrict addrlen)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << src_addr << ", " << addrlen << ")");
        auto ret = PROXY(recvfrom)(sockfd, buf, len, flags, src_addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << src_addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << msg << ", " << getMSGFlags(flags) << ")");
        auto ret = PROXY(recvmsg)(sockfd, msg, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msg << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    ssize_t send(int sockfd, const void *buf, size_t len, int flags)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << buf << ", " << len << ", " << flags << ")");
        auto ret = PROXY(send)(sockfd, buf, len, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << dest_addr << ", " << addrlen << ")");
        auto ret = PROXY(sendto)(sockfd, buf, len, flags, dest_addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << buf << ", " << len << ", " << flags << ", " << dest_addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << msg << ", " << getMSGFlags(flags) << ")");
        auto ret = PROXY(sendmsg)(sockfd, msg, flags);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << msg << ", " << getMSGFlags(flags) << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int shutdown(int sockfd, int how)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << getShutdownHow(how) << ")");
        auto ret = PROXY(shutdown)(sockfd, how);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << getShutdownHow(how) << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int getpeername(int sockfd, struct sockaddr *__restrict addr, socklen_t *__restrict addrlen)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << addr << ", " << addrlen << ")");
        auto ret = PROXY(getpeername)(sockfd, addr, addrlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << addr << ", " << addrlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *__restrict optlen)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ")");
        auto ret = PROXY(getsockopt)(sockfd, level, optname, optval, optlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
    {
        debug("[LFI LD_PRELOAD] Start (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ")");
        auto ret = PROXY(setsockopt)(sockfd, level, optname, optval, optlen);
        debug("[LFI LD_PRELOAD] End (" << sockfd << ", " << getSocketLevel(level) << ", " << getSocketOptionName(level, optname) << ", " << getSocketOptval(optval, optlen) << ", " << optlen << ") = " << ret << " " << STR_ERRNO);
        return ret;
    }

    // int epoll_create(int size)
    // {
    //     debug("[LFI LD_PRELOAD] Start (" << size << ")");
    //     auto ret = PROXY(epoll_create)(size);
    //     debug("[LFI LD_PRELOAD] End (" << size << ") = " << ret << " " << STR_ERRNO);
    //     return ret;
    // }

    // int epoll_create1(int flags)
    // {
    //     debug("[LFI LD_PRELOAD] Start (" << getAcceptFlags(flags) << ")");
    //     auto ret = PROXY(epoll_create1)(flags);
    //     debug("[LFI LD_PRELOAD] End (" << getAcceptFlags(flags) << ") = " << ret << " " << STR_ERRNO);
    //     return ret;
    // }

    // int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
    // {
    //     debug("[LFI LD_PRELOAD] Start (" << epfd << ", " << op << ", " << fd << ", " << getEPollEventStr(event, 1) << ")");
    //     auto ret = PROXY(epoll_ctl)(epfd, op, fd, event);
    //     debug("[LFI LD_PRELOAD] End (" << epfd << ", " << op << ", " << fd << ", " << getEPollEventStr(event, 1) << ") = " << ret << " " << STR_ERRNO);
    //     return ret;
    // }

    // int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
    // {
    //     debug("[LFI LD_PRELOAD] Start (" << epfd << ", " << getEPollEventStr(events, maxevents) << ", " << maxevents << ", " << timeout << ")");
    //     auto ret = PROXY(epoll_wait)(epfd, events, maxevents, timeout);
    //     debug("[LFI LD_PRELOAD] End (" << epfd << ", " << getEPollEventStr(events, maxevents) << ", " << maxevents << ", " << timeout << ") = " << ret << " " << STR_ERRNO);
    //     return ret;
    // }

    // int poll(struct pollfd *fds, nfds_t nfds, int timeout)
    // {
    //     debug("[LFI LD_PRELOAD] Start (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << timeout << ")");
    //     auto ret = PROXY(poll)(fds, nfds, timeout);
    //     debug("[LFI LD_PRELOAD] End (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << timeout << ") = " << ret << " " << STR_ERRNO);
    //     return ret;
    // }

    // int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const sigset_t *sigmask)
    // {
    //     debug("[LFI LD_PRELOAD] Start (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << tmo_p << ", " << sigmask << ")");
    //     auto ret = PROXY(ppoll)(fds, nfds, tmo_p, sigmask);
    //     debug("[LFI LD_PRELOAD] End (" << getPollfdStr(fds, nfds) << ", " << nfds << ", " << tmo_p << ", " << sigmask << ") = " << ret << " " << STR_ERRNO);
    //     return ret;
    // }

    // int select(int nfds, fd_set *__restrict readfds, fd_set *__restrict writefds, fd_set *__restrict exceptfds, struct timeval *__restrict timeout)
    // {
    //     debug("[LFI LD_PRELOAD] Start (" << nfds << ", " << readfds << ", " << writefds << ", " << exceptfds << ", " << timeout << ")");
    //     auto ret = PROXY(select)(nfds, readfds, writefds, exceptfds, timeout);
    //     debug("[LFI LD_PRELOAD] End (" << nfds << ", " << readfds << ", " << writefds << ", " << exceptfds << ", " << timeout << ") = " << ret << " " << STR_ERRNO);
    //     return ret;
    // }

#ifdef __cplusplus
}
#endif