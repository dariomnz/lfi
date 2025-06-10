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

#include <aio.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <chrono>
#include <coroutine>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#include "impl/debug.hpp"
#include "lfi.h"
#include "lfi_async.h"
#include "lfi_error.h"

template <typename K, typename V>
struct double_vector {
   private:
    std::vector<K> m_vec1;
    std::vector<V> m_vec2;

   public:
    std::mutex m_mutex;

    size_t size1() { return m_vec1.size(); }
    K* data1() { return m_vec1.data(); }
    size_t size2() { return m_vec1.size(); }
    K* data2() { return m_vec1.data(); }
    K get1(size_t pos) { return m_vec1[pos]; }
    V get2(size_t pos) { return m_vec2[pos]; }

    void append(K value1, V value2) {
        m_vec1.push_back(value1);
        m_vec2.push_back(value2);
    }

    void remove(size_t pos) {
        std::unique_lock lock(m_mutex);
        std::iter_swap(m_vec1.begin() + pos, m_vec1.end() - 1);
        m_vec1.erase(m_vec1.end() - 1);
        std::iter_swap(m_vec2.begin() + pos, m_vec2.end() - 1);
        m_vec2.erase(m_vec2.end() - 1);
    }
};

class progress_loop {
   public:
    static progress_loop& instance() {
        static progress_loop loop;
        return loop;
    }

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
                    auto handle_ptr = events[i].data.ptr;
                    if (handle_ptr) {
                        std::coroutine_handle<>::from_address(handle_ptr).resume();
                    }
                }
            }
            {
                std::unique_lock lock(m_waiting_aio.m_mutex);
                int completed = -1;
                for (size_t i = 0; i < m_waiting_aio.size1(); i++) {
                    int status = aio_error(m_waiting_aio.get1(i));
                    switch (status) {
                        case 0:
                        case ECANCELED:
                            completed = i;
                            break;
                        case EINPROGRESS:
                            break;
                        default:
                            print("Error on aio_error " << status << " " << strerror(status));
                            completed = i;
                            break;
                    }
                }
                if (completed != -1) {
                    auto handle_ptr = m_waiting_aio.get2(completed);
                    lock.unlock();
                    m_waiting_aio.remove(completed);
                    if (handle_ptr) {
                        std::coroutine_handle<>::from_address(handle_ptr).resume();
                    }
                }
            }
            {
                std::unique_lock lock(m_waiting_req.m_mutex);
                if (m_waiting_req.size1() == 0) continue;
                int completed = lfi_wait_any_t(m_waiting_req.data1(), m_waiting_req.size1(), 0);

                if (completed < 0) {
                    if (completed == -LFI_TIMEOUT) continue;
                    std::cerr << "Error in lfi_wait_any " << completed << " " << lfi_strerror(completed);
                } else {
                    auto handle_ptr = m_waiting_req.get2(completed);
                    lock.unlock();
                    m_waiting_req.remove(completed);
                    if (handle_ptr) {
                        std::coroutine_handle<>::from_address(handle_ptr).resume();
                    }
                }
            }
        }
    }

    int getEpollFd() const { return m_epoll_fd; }
    auto& get_waiting_req() { return m_waiting_req; }
    auto& get_waiting_aio() { return m_waiting_aio; }

   private:
    int m_epoll_fd;
    double_vector<lfi_request*, void*> m_waiting_req;
    double_vector<aiocb*, void*> m_waiting_aio;

    progress_loop() {
        m_epoll_fd = epoll_create1(0);
        if (m_epoll_fd == -1) {
            perror("Error al crear epoll");
            exit(EXIT_FAILURE);
        }
    }

    ~progress_loop() { close(m_epoll_fd); }

    progress_loop(const progress_loop&) = delete;
    progress_loop& operator=(const progress_loop&) = delete;
    progress_loop(const progress_loop&&) = delete;
    progress_loop&& operator=(const progress_loop&&) = delete;
};

struct ReqAwaitable {
   protected:
    bool m_request_to_free = false;
    lfi_request* m_request = nullptr;
    int m_comm_id = -1;
    void* m_buffer = nullptr;
    size_t m_size = 0;
    int m_tag = 0;
    int m_error = 0;
    ReqAwaitable(int comm_id, void* buffer, size_t size, int tag)
        : m_comm_id(comm_id), m_buffer(buffer), m_size(size), m_tag(tag) {}
    ReqAwaitable(lfi_request* request, void* buffer, size_t size, int tag)
        : m_request(request), m_buffer(buffer), m_size(size), m_tag(tag) {}

   public:
    ~ReqAwaitable() {
        if (m_request_to_free) {
            lfi_request_free(m_request);
        }
    }

    bool await_ready() const noexcept {
        if (m_error < 0) return true;
        return lfi_request_completed(m_request);
    }

    void await_suspend(std::coroutine_handle<> handle) const noexcept {
        progress_loop::instance().get_waiting_req().append(m_request, handle.address());
    }

    ssize_t await_resume() const noexcept {
        if (m_error < 0) return m_error;
        auto req_error = lfi_request_error(m_request);
        if (req_error < 0) return req_error;
        return lfi_request_size(m_request);
    }
};

struct RecvAwaitable : public ReqAwaitable {
    RecvAwaitable(int comm_id, void* buffer, size_t size, int tag = 0) : ReqAwaitable(comm_id, buffer, size, tag) {
        m_request = lfi_request_create(m_comm_id);
        m_request_to_free = true;
        m_error = lfi_trecv_async(m_request, buffer, size, tag);
    }
    RecvAwaitable(lfi_request* request, void* buffer, size_t size, int tag = 0)
        : ReqAwaitable(request, buffer, size, tag) {
        m_error = lfi_trecv_async(m_request, buffer, size, tag);
    }
};

struct SendAwaitable : public ReqAwaitable {
    SendAwaitable(int comm_id, void* buffer, size_t size, int tag = 0) : ReqAwaitable(comm_id, buffer, size, tag) {
        m_request = lfi_request_create(m_comm_id);
        m_request_to_free = true;
        m_error = lfi_tsend_async(m_request, buffer, size, tag);
    }
    SendAwaitable(lfi_request* request, void* buffer, size_t size, int tag = 0)
        : ReqAwaitable(request, buffer, size, tag) {
        m_error = lfi_tsend_async(m_request, buffer, size, tag);
    }
};

struct AcceptAwaitable {
    int m_server_id;
    int m_epoll_fd;

    AcceptAwaitable(int server_id, int epoll_fd) : m_server_id(server_id), m_epoll_fd(epoll_fd) {}

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

struct AIOAwaitable {
   protected:
    int m_fd = -1;
    void* m_buff = nullptr;
    size_t m_size = 0;
    off_t m_offset = 0;
    aiocb m_aiocb = {};
    int m_error = 0;

    AIOAwaitable(int fd, void* buff, size_t size, off_t offset)
        : m_fd(fd), m_buff(buff), m_size(size), m_offset(offset) {}

   public:
    bool await_ready() const noexcept {
        if (m_error < 0) return true;
        return aio_error(&m_aiocb) != EINPROGRESS;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
        progress_loop::instance().get_waiting_aio().append(&m_aiocb, handle.address());
    }

    int await_resume() noexcept {
        if (m_error < 0) return m_error;
        return aio_return(&m_aiocb);
    }
};

struct AIOReadAwaitable : public AIOAwaitable {
    AIOReadAwaitable(int fd, void* buff, size_t size, off_t offset) : AIOAwaitable(fd, buff, size, offset) {
        int ret = 0;
        m_aiocb.aio_fildes = m_fd;
        m_aiocb.aio_buf = m_buff;
        m_aiocb.aio_nbytes = m_size;
        m_aiocb.aio_offset = m_offset;
        ret = aio_read(&m_aiocb);
        if (ret < 0) {
            m_error = ret;
            print_error("Error in aio_read");
        }
    }
};

struct AIOWriteAwaitable : public AIOAwaitable {
    AIOWriteAwaitable(int fd, void* buff, size_t size, off_t offset) : AIOAwaitable(fd, buff, size, offset) {
        int ret = 0;
        m_aiocb.aio_fildes = m_fd;
        m_aiocb.aio_buf = m_buff;
        m_aiocb.aio_nbytes = m_size;
        m_aiocb.aio_offset = m_offset;
        ret = aio_write(&m_aiocb);
        if (ret < 0) {
            m_error = ret;
            print_error("Error in aio_write");
        }
    }
};

struct AIOFsyncAwaitable : public AIOAwaitable {
    AIOFsyncAwaitable(int fd, void* buff, size_t size, off_t offset) : AIOAwaitable(fd, buff, size, offset) {
        int ret = 0;
        m_aiocb.aio_fildes = m_fd;
        ret = aio_fsync(O_SYNC, &m_aiocb);
        if (ret < 0) {
            m_error = ret;
            print_error("Error in aio_fsync");
        }
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
        std::vector<char> buffer(1024, '\0');
        lfi_request* req = lfi_request_create(clientID);
        ssize_t ret = 0;
        while (true) {
            ret = co_await RecvAwaitable(req, buffer.data(), buffer.size());
            if (ret < 0) {
                std::cerr << "Error in lfi_recv_async " << ret << " " << lfi_strerror(ret);
            }
            std::cout << "Received: " << std::string_view(buffer.data(), ret) << std::endl;

            std::string file_path(buffer.data(), ret);
            int fd = open(file_path.c_str(), O_RDONLY);
            if (fd < 0) {
                print_error("Error opening file " << file_path);
                ssize_t err = fd;
                ret = co_await SendAwaitable(req, &err, sizeof(err));
                if (ret < 0) {
                    std::cerr << "Error in lfi_send_async " << ret << " " << lfi_strerror(ret);
                }
                print_error("Send error");
                continue;
            }

            ssize_t readed = 0;
            ssize_t total_readed = 0;
            while ((readed = co_await AIOReadAwaitable(fd, buffer.data(), buffer.size(), total_readed)) > 0) {
                ret = co_await SendAwaitable(req, &readed, sizeof(readed));
                if (ret < 0) {
                    std::cerr << "Error in lfi_send_async " << ret << " " << lfi_strerror(ret);
                }
                ret = co_await SendAwaitable(req, buffer.data(), readed);
                if (ret < 0) {
                    std::cerr << "Error in lfi_send_async " << ret << " " << lfi_strerror(ret);
                }
                total_readed += readed;
            }
            // Send 0 to finish
            ret = co_await SendAwaitable(req, &readed, sizeof(readed));
            if (ret < 0) {
                std::cerr << "Error in lfi_send_async " << ret << " " << lfi_strerror(ret);
            }
            ret = close(fd);
            if (ret < 0) {
                print_error("Error closing file " << file_path);
            }
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

int main() {
    int port = 8080;
    int server_id = lfi_server_create(nullptr, &port);
    if (server_id == -1) {
        perror("Error al crear el socket");
        return 1;
    }

    std::cout << "Servidor escuchando en el puerto " << port << "..." << std::endl;

    progress_loop& loop = progress_loop::instance();

    AcceptTask::acceptConnections(server_id, loop.getEpollFd());

    loop.run();

    lfi_server_close(server_id);
    return 0;
}
