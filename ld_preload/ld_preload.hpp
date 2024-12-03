
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

#ifndef _LD_PRELOAD_H
#define _LD_PRELOAD_H

#include <sys/types.h>
#include <sys/socket.h>
#include <unordered_set>

class ld_preload
{
public:
    std::unordered_set<int> socket_ids;
    std::unordered_set<int> lfi_ids;

    static ld_preload& get_instance(){
        static ld_preload instance;
        return instance;
    }
};

#ifdef __cplusplus
extern "C" {
#endif

int socket(int domain, int type, int protocol);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);

int close(int fd);

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);

int accept(int sockfd, struct sockaddr *__restrict addr, socklen_t *__restrict addrlen);
int accept4(int sockfd, struct sockaddr *__restrict addr, socklen_t *__restrict addrlen, int flags);

ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t recvfrom(int sockfd, void *__restrict buf, size_t len, int flags, struct sockaddr *__restrict src_addr, socklen_t *__restrict addrlen);
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);

ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);

int select(int nfds, fd_set *__restrict readfds, fd_set *__restrict writefds, fd_set *__restrict exceptfds, struct timeval *__restrict timeout);

int shutdown(int sockfd, int how);

int getpeername(int sockfd, struct sockaddr *__restrict addr, socklen_t *__restrict addrlen);

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *__restrict optlen);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);

#ifdef __cplusplus
}
#endif

#endif // _LD_PRELOAD_H