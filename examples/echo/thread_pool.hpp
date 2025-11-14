
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

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
   public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency() * 2) : stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this]() {
                while (true) {
                    std::function<int()> task;

                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        condition.wait(lock, [this]() { return stop || !tasks.empty(); });

                        if (stop && tasks.empty()) return;

                        task = std::move(tasks.front());
                        tasks.pop();
                    }

                    task();
                }
            });
        }
    }

    void enqueue(std::function<int()> func) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (stop) throw std::runtime_error("No se pueden agregar tareas a un ThreadPool detenido");
            tasks.emplace(std::move(func));
            condition.notify_one();
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
            condition.notify_all();
        }

        for (auto &t : workers) t.join();
    }

   private:
    std::vector<std::thread> workers;
    std::queue<std::function<int()>> tasks;

    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
};