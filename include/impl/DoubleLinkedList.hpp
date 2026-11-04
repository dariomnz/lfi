
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
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <type_traits>

namespace LFI {

template <typename T>
struct DoubleLinkedList {
    T* head = nullptr;
    T* tail = nullptr;
    std::size_t size = 0;

    // static_assert to ensure T has 'prev', 'next' and 'inserted' members
    static_assert(std::is_same_v<decltype(T::next), T*>, "Type T must contain a member: T* next");
    static_assert(std::is_same_v<decltype(T::prev), T*>, "Type T must contain a member: T* prev");
    static_assert(std::is_same_v<decltype(T::inserted), bool>, "Type T must contain a member: bool inserted");

    struct iterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T*;

        T* node = nullptr;

        inline T* operator*() const { return node; }
        inline pointer operator->() const { return node; }

        inline iterator& operator++() {
            node = node->next;
            return *this;
        }
        inline iterator operator++(int) {
            iterator tmp = *this;
            node = node->next;
            return tmp;
        }

        friend bool operator==(const iterator& a, const iterator& b) { return a.node == b.node; }
        friend bool operator!=(const iterator& a, const iterator& b) { return a.node != b.node; }
    };

    inline iterator begin() { return iterator{head}; }
    inline iterator end() { return iterator{nullptr}; }

    inline void push_back(T* node) {
        if (!node) return;
        // if (node->inserted) std::abort();

        if (tail == nullptr) {
            head = tail = node;
        } else {
            tail->next = node;
            node->prev = tail;
            tail = node;
        }
        node->inserted = true;
        size++;
    }

    inline void push_front(T* node) {
        if (!node) return;
        // if (node->inserted) std::abort();

        if (head == nullptr) {
            head = tail = node;
        } else {
            head->prev = node;
            node->next = head;
            head = node;
        }
        node->inserted = true;
        size++;
    }

    inline iterator erase(T* node) {
        if (!node) return iterator{nullptr};
        // if (!node->inserted) std::abort();

        T* next = node->next;

        if (node->prev) {
            node->prev->next = node->next;
        } else {
            head = node->next;
        }

        if (node->next) {
            node->next->prev = node->prev;
        } else {
            tail = node->prev;
        }

        node->inserted = false;
        node->next = nullptr;
        node->prev = nullptr;
        size--;

        return iterator{next};
    }

    inline iterator erase(iterator pos) {
        return erase(pos.node);
    }
};

}  // namespace LFI