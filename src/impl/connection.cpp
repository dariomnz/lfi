
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

#include "impl/debug.hpp"
#include "impl/lfi.hpp"
#include "impl/ns.hpp"
#include "impl/socket.hpp"

namespace LFI {

int LFI::init_server(int socket, int32_t comm_id) {
    int ret;
    debug_info("[LFI] Start");

    // First comunicate the identifier
    std::string host_id = ns::get_host_name() + " " + ns::get_host_ip();
    std::string peer_id;

    // Server send
    size_t host_id_size = host_id.size();
    ret = socket::send(socket, &host_id_size, sizeof(host_id_size));
    if (ret != sizeof(host_id_size)) {
        print_error("socket::send host_id_size socket " << socket);
        return -1;
    }
    ret = socket::send(socket, host_id.data(), host_id.size());
    if (ret != static_cast<int>(host_id.size())) {
        print_error("socket::send host_id.data() socket " << socket);
        return -1;
    }

    // Server recv
    size_t peer_id_size = 0;
    ret = socket::recv(socket, &peer_id_size, sizeof(peer_id_size));
    if (ret != sizeof(peer_id_size)) {
        print_error("socket::recv peer_id_size socket " << socket);
        return -1;
    }
    peer_id.resize(peer_id_size);
    ret = socket::recv(socket, peer_id.data(), peer_id.size());
    if (ret != static_cast<int>(peer_id.size())) {
        print_error("socket::recv peer_id.data() socket " << socket);
        return -1;
    }

    debug_info("[LFI] host_id " << host_id << " peer_id " << peer_id);

    // Initialize endpoints
    bool is_shm = host_id == peer_id;
    std::shared_ptr<lfi_comm> comm = init_comm(is_shm, comm_id);

    // Exchange ranks
    ret = socket::recv(socket, &comm->rank_self_in_peer, sizeof(comm->rank_self_in_peer));
    if (ret != sizeof(comm->rank_self_in_peer)) {
        print_error("socket::recv comm->rank_self_in_peer socket " << socket);
        return -1;
    }
    ret = socket::send(socket, &comm->rank_peer, sizeof(comm->rank_peer));
    if (ret != sizeof(comm->rank_peer)) {
        print_error("socket::send comm.rank_peer socket " << socket);
        return -1;
    }

    // Exchange addr
    std::vector<uint8_t> host_addr;
    std::vector<uint8_t> peer_addr;
    size_t peer_addr_size = 0;
    debug_info("[LFI] recv addr");
    ret = socket::recv(socket, &peer_addr_size, sizeof(peer_addr_size));
    if (ret != sizeof(peer_addr_size)) {
        print_error("socket::recv peer_addr_size socket " << socket);
        return -1;
    }
    peer_addr.resize(peer_addr_size);
    ret = socket::recv(socket, peer_addr.data(), peer_addr.size());
    if (ret != static_cast<int>(peer_addr.size())) {
        print_error("socket::recv peer_addr.data() socket " << socket);
        return -1;
    }
    ret = register_addr(comm, peer_addr);
    if (ret < 0) {
        print_error("register_addr");
        return ret;
    }

    ret = get_addr(comm, host_addr);
    if (ret < 0) {
        print_error("get_addr");
        return ret;
    }
    debug_info("[LFI] send addr");
    size_t host_addr_size = host_addr.size();
    ret = socket::send(socket, &host_addr_size, sizeof(host_addr_size));
    if (ret != sizeof(host_addr_size)) {
        print_error("socket::send host_addr_size socket " << socket);
        return -1;
    }
    ret = socket::send(socket, host_addr.data(), host_addr.size());
    if (ret != static_cast<int>(host_addr.size())) {
        print_error("socket::send host_addr.data() socket " << socket);
        return -1;
    }

    ret = comm->rank_peer;
    comm->is_ready = true;

    // Do a send recv because some providers need it
    int buf = 123;
    lfi_msg msg;
    msg = LFI::send(comm->rank_peer, &buf, sizeof(buf), 123);
    if (msg.error < 0) {
        print_error("LFI::send");
        return msg.error;
    }
    msg = LFI::recv(comm->rank_peer, &buf, sizeof(buf), 1234);
    if (msg.error < 0) {
        print_error("LFI::recv");
        return msg.error;
    }

    comm->in_fut = true;

    debug_info("[LFI] End = " << ret);
    return ret;
}

int LFI::init_client(int socket, int32_t comm_id) {
    int ret;
    debug_info("[LFI] Start");

    // First comunicate the identifier
    std::string host_id = ns::get_host_name() + " " + ns::get_host_ip();
    std::string peer_id;

    // Client recv
    size_t peer_id_size = 0;
    ret = socket::recv(socket, &peer_id_size, sizeof(peer_id_size));
    if (ret != sizeof(peer_id_size)) {
        print_error("socket::recv peer_id_size socket " << socket);
        return -1;
    }
    peer_id.resize(peer_id_size);
    ret = socket::recv(socket, peer_id.data(), peer_id.size());
    if (ret != static_cast<int>(peer_id.size())) {
        print_error("socket::recv peer_id.data() socket " << socket);
        return -1;
    }

    // Client send
    size_t host_id_size = host_id.size();
    ret = socket::send(socket, &host_id_size, sizeof(host_id_size));
    if (ret != sizeof(host_id_size)) {
        print_error("socket::send host_id_size socket " << socket);
        return -1;
    }
    ret = socket::send(socket, host_id.data(), host_id.size());
    if (ret != static_cast<int>(host_id.size())) {
        print_error("socket::send host_id.data() socket " << socket);
        return -1;
    }

    debug_info("[LFI] host_id " << host_id << " peer_id " << peer_id);

    // Initialize endpoints
    bool is_shm = host_id == peer_id;
    std::shared_ptr<lfi_comm> comm = init_comm(is_shm, comm_id);

    // Exchange ranks
    ret = socket::send(socket, &comm->rank_peer, sizeof(comm->rank_peer));
    if (ret != sizeof(comm->rank_peer)) {
        print_error("socket::send comm->rank_peer socket " << socket);
        return -1;
    }
    ret = socket::recv(socket, &comm->rank_self_in_peer, sizeof(comm->rank_self_in_peer));
    if (ret != sizeof(comm->rank_self_in_peer)) {
        print_error("socket::recv comm->rank_self_in_peer socket " << socket);
        return -1;
    }

    // Exchange addr
    std::vector<uint8_t> host_addr;
    std::vector<uint8_t> peer_addr;
    size_t peer_addr_size = 0;

    ret = get_addr(comm, host_addr);
    if (ret < 0) {
        print_error("get_addr");
        return ret;
    }
    debug_info("[LFI] send addr");
    size_t host_addr_size = host_addr.size();
    ret = socket::send(socket, &host_addr_size, sizeof(host_addr_size));
    if (ret != sizeof(host_addr_size)) {
        print_error("socket::send host_addr_size socket " << socket);
        return -1;
    }
    ret = socket::send(socket, host_addr.data(), host_addr.size());
    if (ret != static_cast<int>(host_addr.size())) {
        print_error("socket::send host_addr.data() socket " << socket);
        return -1;
    }

    debug_info("[LFI] recv addr");
    ret = socket::recv(socket, &peer_addr_size, sizeof(peer_addr_size));
    if (ret != sizeof(peer_addr_size)) {
        print_error("socket::recv peer_addr_size socket " << socket);
        return -1;
    }
    peer_addr.resize(peer_addr_size);
    ret = socket::recv(socket, peer_addr.data(), peer_addr.size());
    if (ret != static_cast<int>(peer_addr.size())) {
        print_error("socket::recv peer_addr.data() socket " << socket);
        return -1;
    }
    ret = register_addr(comm, peer_addr);
    if (ret < 0) {
        print_error("register_addr");
        return ret;
    }

    ret = comm->rank_peer;
    comm->is_ready = true;

    // Do a recv send because some providers need it
    int buf = 123;
    lfi_msg msg;
    msg = LFI::recv(comm->rank_peer, &buf, sizeof(buf), 123);
    if (msg.error < 0) {
        print_error("LFI::recv");
        return msg.error;
    }
    msg = LFI::send(comm->rank_peer, &buf, sizeof(buf), 1234);
    if (msg.error < 0) {
        print_error("LFI::send");
        return msg.error;
    }

    comm->in_fut = true;

    debug_info("[LFI] End = " << ret);
    return ret;
}
}  // namespace LFI