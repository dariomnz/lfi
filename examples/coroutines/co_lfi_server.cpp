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

#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <chrono>
#include <coroutine>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

#include "lfi.h"
#include "lfi_async.h"
#include "lfi_error.h"

std::mutex waiting_req_mutex;
std::vector<lfi_request*> waiting_req;
std::vector<void*> waiting_req_co;

template <typename T>
void remove_unsorted(std::vector<T>& vec, size_t pos) {
    std::iter_swap(vec.begin() + pos, vec.end() - 1);
    vec.erase(vec.end() - 1);
}

struct RequetsAwaitable {
    lfi_request* m_request;

    RequetsAwaitable(lfi_request* req) : m_request(req) {}

    bool await_ready() const noexcept { return lfi_request_completed(m_request); }

    void await_suspend(std::coroutine_handle<> handle) const noexcept {
        std::unique_lock lock(waiting_req_mutex);
        waiting_req.push_back(m_request);
        waiting_req_co.push_back(handle.address());
        // std::cout << "Added Waiting_req size " << waiting_req.size() << " co size " << waiting_req_co.size()
        //   << std::endl;
    }

    void await_resume() const noexcept {}
};

struct AcceptAwaitable {
    AcceptAwaitable(int server_id, int epoll_fd) : m_server_id(server_id), m_epoll_fd(epoll_fd) {}
    int m_server_id;
    int m_epoll_fd;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
        epoll_event event{};
        event.events = EPOLLIN | EPOLLONESHOT;
        event.data.ptr = handle.address();

        if (epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, m_server_id, &event) == -1) {
            if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_server_id, &event) == -1) {
                perror("Error al registrar socket de servidor en epoll");
                handle.destroy();
            }
        }
    }

    int await_resume() noexcept {
        int clientSocket = lfi_server_accept(m_server_id);
        if (clientSocket == -1) {
            perror("Error al aceptar conexión");
        }
        return clientSocket;
    }
};

struct ClientTask {
    struct promise_type {
        ClientTask get_return_object() { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };

    static ClientTask handleClient(int clientID) {
        constexpr size_t buff_size = 1024;
        char buffer[buff_size];
        lfi_request* req = lfi_request_create(clientID);
        while (true) {
            ssize_t ret = lfi_recv_async(req, buffer, buff_size);
            if (ret < 0) {
                std::cerr << "Error in lfi_recv_async " << ret << " " << lfi_strerror(ret);
            }

            std::cout << "await recv" << std::endl;
            co_await RequetsAwaitable(req);
            std::cout << "Received: " << std::string(buffer, lfi_request_size(req)) << std::endl;
            size_t received = lfi_request_size(req);
            ret = lfi_send_async(req, buffer, received);
            if (ret < 0) {
                std::cerr << "Error in lfi_send_async " << ret << " " << lfi_strerror(ret);
            }

            std::cout << "await send" << std::endl;
            co_await RequetsAwaitable(req);
            std::cout << "Sended: " << std::string(buffer, lfi_request_size(req)) << std::endl;
        }

        lfi_client_close(clientID);
        std::cout << "Client disconected" << std::endl;
    }
};

struct AcceptTask {
    struct promise_type {
        AcceptTask get_return_object() { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };

    static AcceptTask acceptConnections(int serverSocket, int epoll_fd) {
        while (true) {
            int clientSocket = co_await AcceptAwaitable{serverSocket, epoll_fd};
            if (clientSocket != -1) {
                std::cout << "Cliente conectado\n";
                ClientTask::handleClient(clientSocket);
            }
        }
    }
};

// Clase para manejar el bucle principal de epoll
class ProgressLoop {
   public:
    static ProgressLoop& instance() {
        static ProgressLoop loop;
        return loop;
    }

    int getEpollFd() const { return m_epoll_fd; }

    void run() {
        constexpr int MAX_EVENTS = 64;
        epoll_event events[MAX_EVENTS];
        while (true) {
            {
                int event_count = epoll_wait(m_epoll_fd, events, MAX_EVENTS, 0);
                if (event_count == -1) {
                    perror("Error en epoll_wait");
                    break;
                }
                for (int i = 0; i < event_count; ++i) {
                    auto* handle_ptr = static_cast<std::coroutine_handle<>*>(events[i].data.ptr);
                    if (handle_ptr) {
                        std::coroutine_handle<>::from_address(handle_ptr).resume();
                    }
                }
            }
            {
                std::unique_lock lock(waiting_req_mutex);
                if (waiting_req.size() == 0) continue;
                int completed = lfi_wait_any_t(waiting_req.data(), waiting_req.size(), 0);

                if (completed < 0) {
                    if (completed == -LFI_TIMEOUT) continue;
                    std::cerr << "Error in lfi_wait_any " << completed << " " << lfi_strerror(completed);
                } else {
                    // std::cout << "completed req" << std::endl;
                    // lfi_request* req_completed = waiting_req[completed];
                    auto* handle_ptr = static_cast<std::coroutine_handle<>*>(waiting_req_co[completed]);
                    remove_unsorted(waiting_req, completed);
                    remove_unsorted(waiting_req_co, completed);
                    // std::cout << "removed Waiting_req size " << waiting_req.size() << " co size "
                    //           << waiting_req_co.size() << std::endl;

                    if (handle_ptr) {
                        lock.unlock();
                        std::coroutine_handle<>::from_address(handle_ptr).resume();
                    }
                }
            }
        }
    }

   private:
    int m_epoll_fd;

    ProgressLoop() {
        m_epoll_fd = epoll_create1(0);
        if (m_epoll_fd == -1) {
            perror("Error al crear epoll");
            exit(EXIT_FAILURE);
        }
    }

    ~ProgressLoop() { close(m_epoll_fd); }

    ProgressLoop(const ProgressLoop&) = delete;
    ProgressLoop& operator=(const ProgressLoop&) = delete;
};

int main() {
    int port = 8080;
    int server_id = lfi_server_create(nullptr, &port);
    if (server_id == -1) {
        perror("Error al crear el socket");
        return 1;
    }

    std::cout << "Servidor escuchando en el puerto " << port << "..." << std::endl;

    ProgressLoop& loop = ProgressLoop::instance();

    AcceptTask::acceptConnections(server_id, loop.getEpollFd());

    loop.run();

    lfi_server_close(server_id);
    return 0;
}
