
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

#pragma once

#include "impl/debug.hpp"
#include <ostream>
#include <sstream>
#include <cstring>
#include <mutex>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/epoll.h>

#ifdef DEBUG
#define debug(out_format)                                                                                                          \
    {                                                                                                                              \
        std::unique_lock internal_debug_lock(::LFI::get_lock());                                                                                \
        std::cerr << "[" << ::LFI::file_name(__FILE__) << ":" << __LINE__ << "] [" << __func__ << "] " << out_format << std::endl << std::flush; \
    }
#else
#define debug(out_format)
#endif

#define CASE_STR_OS(name) \
    case name:            \
    {                     \
        os << #name;      \
        break;            \
    }

#define CASE_STR_RET(name) \
    case name:             \
    {                      \
        return #name;      \
    }

#define STR_ERRNO (ret < 0 ? std::strerror(errno) : "")

std::ostream &operator<<(std::ostream &os, const socklen_t *addlen)
{
    if (!addlen)
    {
        os << "null";
        return os;
    }
    os << *addlen;
    return os;
}

std::string getAcceptFlags(int flags)
{
    std::string result;

    if (flags & SOCK_NONBLOCK)
        result += "SOCK_NONBLOCK | ";
    if (flags & SOCK_CLOEXEC)
        result += "SOCK_CLOEXEC | ";

    if (result.empty())
    {
        result = "No flags";
    }
    else
    {
        result = result.substr(0, result.size() - 3);
    }

    return result;
}

std::string sockaddr_to_str(const struct sockaddr *addr)
{
    if (addr == nullptr)
    {
        return "";
    }

    char buffer[INET6_ADDRSTRLEN] = {};
    switch (addr->sa_family)
    {
    case AF_INET:
    {
        // IPv4
        const struct sockaddr_in *addr_in = reinterpret_cast<const struct sockaddr_in *>(addr);
        inet_ntop(AF_INET, &(addr_in->sin_addr), buffer, sizeof(buffer));
        return buffer;
    }
    case AF_INET6:
    {
        // IPv6
        const struct sockaddr_in6 *addr_in6 = reinterpret_cast<const struct sockaddr_in6 *>(addr);
        inet_ntop(AF_INET6, &(addr_in6->sin6_addr), buffer, sizeof(buffer));
        return buffer;
    }
    }

    throw std::runtime_error("In sockaddr_to_str, addr->sa_family not AF_INET or AF_INET6");
    return "";
}

std::ostream &operator<<(std::ostream &os, const struct sockaddr *addr)
{
    if (!addr)
    {
        os << "null";
        return os;
    }

    char buffer[INET6_ADDRSTRLEN] = {};

    switch (addr->sa_family)
    {
    case AF_INET:
    {
        // IPv4
        const struct sockaddr_in *addr_in = reinterpret_cast<const struct sockaddr_in *>(addr);
        inet_ntop(AF_INET, &(addr_in->sin_addr), buffer, sizeof(buffer));
        os << "IPv4: " << buffer << ":" << ntohs(addr_in->sin_port);
        break;
    }
    case AF_INET6:
    {
        // IPv6
        const struct sockaddr_in6 *addr_in6 = reinterpret_cast<const struct sockaddr_in6 *>(addr);
        inet_ntop(AF_INET6, &(addr_in6->sin6_addr), buffer, sizeof(buffer));
        os << "IPv6: " << buffer << ":" << ntohs(addr_in6->sin6_port);
        break;
    }
        CASE_STR_OS(AF_UNSPEC);
        CASE_STR_OS(AF_LOCAL);
        CASE_STR_OS(AF_AX25);
        CASE_STR_OS(AF_IPX);
        CASE_STR_OS(AF_APPLETALK);
        CASE_STR_OS(AF_NETROM);
        CASE_STR_OS(AF_BRIDGE);
        CASE_STR_OS(AF_ATMPVC);
        CASE_STR_OS(AF_X25);
        CASE_STR_OS(AF_ROSE);
        CASE_STR_OS(AF_DECnet);
        CASE_STR_OS(AF_NETBEUI);
        CASE_STR_OS(AF_SECURITY);
        CASE_STR_OS(AF_KEY);
        CASE_STR_OS(AF_NETLINK);
        CASE_STR_OS(AF_PACKET);
        CASE_STR_OS(AF_ASH);
        CASE_STR_OS(AF_ECONET);
        CASE_STR_OS(AF_ATMSVC);
        CASE_STR_OS(AF_RDS);
        CASE_STR_OS(AF_SNA);
        CASE_STR_OS(AF_IRDA);
        CASE_STR_OS(AF_PPPOX);
        CASE_STR_OS(AF_WANPIPE);
        CASE_STR_OS(AF_LLC);
        CASE_STR_OS(AF_IB);
        CASE_STR_OS(AF_MPLS);
        CASE_STR_OS(AF_CAN);
        CASE_STR_OS(AF_TIPC);
        CASE_STR_OS(AF_BLUETOOTH);
        CASE_STR_OS(AF_IUCV);
        CASE_STR_OS(AF_RXRPC);
        CASE_STR_OS(AF_ISDN);
        CASE_STR_OS(AF_PHONET);
        CASE_STR_OS(AF_IEEE802154);
        CASE_STR_OS(AF_CAIF);
        CASE_STR_OS(AF_ALG);
        CASE_STR_OS(AF_NFC);
        CASE_STR_OS(AF_VSOCK);
        CASE_STR_OS(AF_KCM);
        CASE_STR_OS(AF_QIPCRTR);
        CASE_STR_OS(AF_SMC);
        CASE_STR_OS(AF_XDP);
        CASE_STR_OS(AF_MCTP);
        CASE_STR_OS(AF_MAX);
    default:
        os << "Unknown family: " << addr->sa_family;
        break;
    }

    return os;
}

std::string getSocketType(int type)
{
    switch (type)
    {
    case SOCK_STREAM:
        return "SOCK_STREAM (TCP)";
    case SOCK_DGRAM:
        return "SOCK_DGRAM (UDP)";
    case SOCK_RAW:
        return "SOCK_RAW";
    default:
        return "Unknown socket type";
    }
}

std::string getProtocol(int protocol)
{
    switch (protocol)
    {
        CASE_STR_RET(IPPROTO_IP)
        CASE_STR_RET(IPPROTO_TCP)
        CASE_STR_RET(IPPROTO_UDP)
        CASE_STR_RET(IPPROTO_ICMP)
        CASE_STR_RET(IPPROTO_RAW)
    default:
        return "Unknown protocol";
    }
}

std::string socket_str(int domain, int type, int protocol)
{
    std::stringstream out;
    out << (domain == AF_INET ? "AF_INET" : domain == AF_INET6 ? "AF_INET6"
                                                               : "Unknown")
        << ", ";
    out << getSocketType(type) << ", ";
    out << getProtocol(protocol);
    return out.str();
}

std::string getShutdownHow(int how)
{
    switch (how)
    {
    case SHUT_RD:
        return "SHUT_RD (Disable Reads)";
    case SHUT_WR:
        return "SHUT_WR (Disable Writes)";
    case SHUT_RDWR:
        return "SHUT_RDWR (Disable Both)";
    default:
        return "Unknown shutdown mode";
    }
}

std::string getSocketLevel(int level)
{
    switch (level)
    {
        CASE_STR_RET(SOL_SOCKET);
        CASE_STR_RET(IPPROTO_TCP);
        CASE_STR_RET(IPPROTO_UDP);
        CASE_STR_RET(IPPROTO_IP);
    default:
        return "Unknown level";
    }
}

std::string getSocketOptionName(int level, int optname)
{
    if (level == SOL_SOCKET)
    {
        switch (optname)
        {
            CASE_STR_RET(SO_DEBUG)
            CASE_STR_RET(SO_REUSEADDR)
            CASE_STR_RET(SO_TYPE)
            CASE_STR_RET(SO_ERROR)
            CASE_STR_RET(SO_DONTROUTE)
            CASE_STR_RET(SO_BROADCAST)
            CASE_STR_RET(SO_SNDBUF)
            CASE_STR_RET(SO_RCVBUF)
            CASE_STR_RET(SO_SNDBUFFORCE)
            CASE_STR_RET(SO_RCVBUFFORCE)
            CASE_STR_RET(SO_KEEPALIVE)
            CASE_STR_RET(SO_OOBINLINE)
            CASE_STR_RET(SO_NO_CHECK)
            CASE_STR_RET(SO_PRIORITY)
            CASE_STR_RET(SO_LINGER)
            CASE_STR_RET(SO_BSDCOMPAT)
            CASE_STR_RET(SO_REUSEPORT)
            CASE_STR_RET(SO_SECURITY_AUTHENTICATION)
            CASE_STR_RET(SO_SECURITY_ENCRYPTION_TRANSPORT)
            CASE_STR_RET(SO_SECURITY_ENCRYPTION_NETWORK)
            CASE_STR_RET(SO_BINDTODEVICE)
            CASE_STR_RET(SO_ATTACH_FILTER)
            CASE_STR_RET(SO_DETACH_FILTER)
            CASE_STR_RET(SO_PEERNAME)
            CASE_STR_RET(SO_ACCEPTCONN)
            CASE_STR_RET(SO_PEERSEC)
            CASE_STR_RET(SO_PASSSEC)
            CASE_STR_RET(SO_MARK)
            CASE_STR_RET(SO_PROTOCOL)
            CASE_STR_RET(SO_RXQ_OVFL)
            CASE_STR_RET(SO_WIFI_STATUS)
            CASE_STR_RET(SO_PEEK_OFF)
            CASE_STR_RET(SO_NOFCS)
            CASE_STR_RET(SO_LOCK_FILTER)
            CASE_STR_RET(SO_SELECT_ERR_QUEUE)
            CASE_STR_RET(SO_BUSY_POLL)
            CASE_STR_RET(SO_MAX_PACING_RATE)
            CASE_STR_RET(SO_BPF_EXTENSIONS)
            CASE_STR_RET(SO_INCOMING_CPU)
            CASE_STR_RET(SO_ATTACH_BPF)
            CASE_STR_RET(SO_ATTACH_REUSEPORT_CBPF)
            CASE_STR_RET(SO_ATTACH_REUSEPORT_EBPF)
            CASE_STR_RET(SO_CNX_ADVICE)
            CASE_STR_RET(SCM_TIMESTAMPING_OPT_STATS)
            CASE_STR_RET(SO_MEMINFO)
            CASE_STR_RET(SO_INCOMING_NAPI_ID)
            CASE_STR_RET(SO_COOKIE)
            CASE_STR_RET(SCM_TIMESTAMPING_PKTINFO)
            CASE_STR_RET(SO_PEERGROUPS)
            CASE_STR_RET(SO_ZEROCOPY)
            CASE_STR_RET(SO_TXTIME)
            CASE_STR_RET(SO_BINDTOIFINDEX)
            CASE_STR_RET(SO_TIMESTAMP_OLD)
            CASE_STR_RET(SO_TIMESTAMPNS_OLD)
            CASE_STR_RET(SO_TIMESTAMPING_OLD)
            CASE_STR_RET(SO_TIMESTAMP_NEW)
            CASE_STR_RET(SO_TIMESTAMPNS_NEW)
            CASE_STR_RET(SO_TIMESTAMPING_NEW)
            CASE_STR_RET(SO_RCVTIMEO_NEW)
            CASE_STR_RET(SO_SNDTIMEO_NEW)
            CASE_STR_RET(SO_DETACH_REUSEPORT_BPF)
            CASE_STR_RET(SO_PREFER_BUSY_POLL)
            CASE_STR_RET(SO_BUSY_POLL_BUDGET)
            CASE_STR_RET(SO_NETNS_COOKIE)
            CASE_STR_RET(SO_BUF_LOCK)
        default:
            return "Unknown socket option " + std::to_string(optname);
        }
    }
    else if (level == IPPROTO_TCP)
    {
        switch (optname)
        {
            CASE_STR_RET(TCP_NODELAY)
            CASE_STR_RET(TCP_MAXSEG)
            CASE_STR_RET(TCP_CORK)
            CASE_STR_RET(TCP_KEEPIDLE)
            CASE_STR_RET(TCP_KEEPINTVL)
            CASE_STR_RET(TCP_KEEPCNT)
            CASE_STR_RET(TCP_SYNCNT)
            CASE_STR_RET(TCP_LINGER2)
            CASE_STR_RET(TCP_DEFER_ACCEPT)
            CASE_STR_RET(TCP_WINDOW_CLAMP)
            CASE_STR_RET(TCP_INFO)
            CASE_STR_RET(TCP_QUICKACK)
            CASE_STR_RET(TCP_CONGESTION)
            CASE_STR_RET(TCP_MD5SIG)
            CASE_STR_RET(TCP_COOKIE_TRANSACTIONS)
            CASE_STR_RET(TCP_THIN_LINEAR_TIMEOUTS)
            CASE_STR_RET(TCP_THIN_DUPACK)
            CASE_STR_RET(TCP_USER_TIMEOUT)
            CASE_STR_RET(TCP_REPAIR)
            CASE_STR_RET(TCP_REPAIR_QUEUE)
            CASE_STR_RET(TCP_QUEUE_SEQ)
            CASE_STR_RET(TCP_REPAIR_OPTIONS)
            CASE_STR_RET(TCP_FASTOPEN)
            CASE_STR_RET(TCP_TIMESTAMP)
            CASE_STR_RET(TCP_NOTSENT_LOWAT)
            CASE_STR_RET(TCP_CC_INFO)
            CASE_STR_RET(TCP_SAVE_SYN)
            CASE_STR_RET(TCP_SAVED_SYN)
            CASE_STR_RET(TCP_REPAIR_WINDOW)
            CASE_STR_RET(TCP_FASTOPEN_CONNECT)
            CASE_STR_RET(TCP_ULP)
            CASE_STR_RET(TCP_MD5SIG_EXT)
            CASE_STR_RET(TCP_FASTOPEN_KEY)
            CASE_STR_RET(TCP_FASTOPEN_NO_COOKIE)
            CASE_STR_RET(TCP_ZEROCOPY_RECEIVE)
            CASE_STR_RET(TCP_INQ)
            CASE_STR_RET(TCP_TX_DELAY)
        default:
            return "Unknown TCP option " + std::to_string(optname);
        }
    }
    return "Unknown option " + std::to_string(optname);
}

std::string getSocketOptval(const void *optval, socklen_t optlen)
{
    std::stringstream out;
    if (optval)
    {
        if (optlen == sizeof(int))
        {
            out << *reinterpret_cast<const int *>(optval);
        }
        else if (optlen == sizeof(struct linger))
        {
            const struct linger *lin = reinterpret_cast<const struct linger *>(optval);
            out << "{ l_onoff: " << lin->l_onoff << ", l_linger: " << lin->l_linger << " }";
        }
        else
        {
            out << "unknown";
        }
    }
    else
    {
        out << "null";
    }
    return out.str();
}

std::string getSocketOptval(const void *optval, socklen_t *optlen)
{
    if (!optlen)
    {
        return "null";
    }
    return getSocketOptval(optval, *optlen);
}

std::string getMSGFlags(int flags)
{
    std::string result;

    if (flags & MSG_EOR)
        result += "MSG_EOR | ";
    if (flags & MSG_OOB)
        result += "MSG_OOB | ";
    if (flags & MSG_DONTROUTE)
        result += "MSG_DONTROUTE | ";
    if (flags & MSG_DONTWAIT)
        result += "MSG_DONTWAIT | ";
    if (flags & MSG_EOR)
        result += "MSG_EOR | ";
    if (flags & MSG_NOSIGNAL)
        result += "MSG_NOSIGNAL | ";
    if (flags & MSG_TRUNC)
        result += "MSG_TRUNC | ";
    if (flags & MSG_CTRUNC)
        result += "MSG_CTRUNC | ";
    if (flags & MSG_WAITALL)
        result += "MSG_WAITALL | ";
    if (flags & MSG_ERRQUEUE)
        result += "MSG_ERRQUEUE | ";

    if (result.empty())
    {
        result = "No flags";
    }
    else
    {
        result = result.substr(0, result.size() - 3);
    }

    return result;
}

std::string printIovec(const struct iovec *iov, size_t iovlen)
{
    std::string result;

    for (size_t i = 0; i < iovlen; ++i)
    {
        // const char* data = static_cast<const char*>(iov[i].iov_base);
        result += "iovec[" + std::to_string(i) + "]: ";
        // result.append(data, iov[i].iov_len);
        result += "(" + std::to_string(iov[i].iov_len) + " bytes) ,";
    }

    return result;
}

std::ostream &operator<<(std::ostream &os, const struct msghdr *msg)
{
    if (!msg)
    {
        os << "null";
        return os;
    }

    os << "Message Header Details: ";

    if (msg->msg_name)
    {
        char buffer[INET6_ADDRSTRLEN] = {0};
        if (msg->msg_namelen == sizeof(struct sockaddr_in))
        {
            struct sockaddr_in *addr = reinterpret_cast<struct sockaddr_in *>(msg->msg_name);
            inet_ntop(AF_INET, &addr->sin_addr, buffer, sizeof(buffer));
            os << "(IPv4): " << buffer << ":" << ntohs(addr->sin_port);
        }
        else if (msg->msg_namelen == sizeof(struct sockaddr_in6))
        {
            struct sockaddr_in6 *addr6 = reinterpret_cast<struct sockaddr_in6 *>(msg->msg_name);
            inet_ntop(AF_INET6, &addr6->sin6_addr, buffer, sizeof(buffer));
            os << "(IPv6): " << buffer << ":" << ntohs(addr6->sin6_port);
        }
        else
        {
            os << "Unknown format";
        }
    }
    else
    {
        os << "null";
    }

    if (msg->msg_iov && msg->msg_iovlen > 0)
    {
        os << "Message Data: " << printIovec(msg->msg_iov, msg->msg_iovlen);
    }
    else
    {
        os << "Message Data: null, ";
    }

    if (msg->msg_control && msg->msg_controllen > 0)
    {
        os << "Control Data: (not implemented, " << msg->msg_controllen << " bytes)";
    }
    else
    {
        os << "Control Data: null";
    }
    os << ", ";

    os << "Message Flags: " << getMSGFlags(msg->msg_flags);

    return os;
}

std::string interpretEvents(short events)
{
    std::string result;

    if (events & POLLIN)
        result += "IN | ";
    if (events & POLLOUT)
        result += "OUT | ";
    if (events & POLLERR)
        result += "ERR | ";
    if (events & POLLHUP)
        result += "HUP | ";
    if (events & POLLNVAL)
        result += "NVAL | ";

    if (!result.empty())
    {
        result = "POLL " + result;
        result = result.substr(0, result.size() - 3);
    }
    else
    {
        result = "No events";
    }

    return result;
}

std::string getPollfdStr(const struct pollfd *fds, nfds_t nfds)
{
    if (!fds)
    {
        return "null";
    }

    std::stringstream out;
    for (nfds_t i = 0; i < nfds; i++)
    {
        out << "Pollfd[" << i << "]:{ ";
        out << "FD: " << fds[i].fd << ", ";
        out << "Events to monitor: " << interpretEvents(fds[i].events) << ", ";
        out << "Events occurred: " << interpretEvents(fds[i].revents) << "}";
    }

    return out.str();
}

std::string interpretEpollEvents(uint32_t events)
{
    std::string result;

    if (events & EPOLLIN)
        result += "IN | ";
    if (events & EPOLLOUT)
        result += "OUT | ";
    if (events & EPOLLERR)
        result += "ERR | ";
    if (events & EPOLLHUP)
        result += "HUP | ";
    if (events & EPOLLRDHUP)
        result += "RDHUP | ";
    if (events & EPOLLET)
        result += "ET | ";
    if (events & EPOLLONESHOT)
        result += "ONESHOT | ";
    if (events & EPOLLWAKEUP)
        result += "WAKEUP | ";
    if (events & EPOLLEXCLUSIVE)
        result += "EXCLUSIVE | ";

    if (!result.empty())
    {
        result = "EPOLL " + result;
        result = result.substr(0, result.size() - 3);
    }
    else
    {
        result = "No events";
    }

    return result;
}

std::string getEPollop(int op)
{
    switch (op)
    {
        CASE_STR_RET(EPOLL_CTL_ADD);
        CASE_STR_RET(EPOLL_CTL_DEL);
        CASE_STR_RET(EPOLL_CTL_MOD);
    default:
        return "";
    }
}

std::string getEPollEventStr(const struct epoll_event *events, int maxevents)
{
    if (!events)
    {
        return "null";
    }

    std::stringstream out;
    for (int i = 0; i < maxevents; i++)
    {
        if (interpretEpollEvents(events[i].events) == "No events")
            continue;
        out << "Epolle[" << i << "]:";
        out << "Events: " << interpretEpollEvents(events[i].events) << ", ";

        out << "Data:";
        out << " ptr " << events[i].data.ptr;
        out << " fd " << events[i].data.fd;
        out << " u32 " << events[i].data.u32;
        out << " u64 " << events[i].data.u64;
        out << " ";
    }

    return out.str();
}