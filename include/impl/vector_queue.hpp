
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

#include <cstring>
#include <vector>

namespace LFI {

template <typename T>
class VectorQueue {
    // Ensure T is safe for raw memory operations (no complex copy constructors/destructors)
    static_assert(std::is_trivially_copyable<T>::value, "Memcpy is only safe for trivially copyable types");

   private:
    std::vector<T> m_buffer;
    size_t m_head = 0, m_tail = 0, m_count = 0;

   public:
    explicit VectorQueue(size_t cap = 64) : m_buffer(cap) {}

    void push(const T& v) {
        if (m_count == m_buffer.size()) {
            size_t oldSize = m_buffer.size();

            m_buffer.resize(oldSize * 2);

            // If the queue is wrapped (the logical end is physically before the start)
            if (m_tail <= m_head) {
                // Before resize: [0...tail-1][head...oldSize-1]
                // After resize:  [0...tail-1][head...oldSize-1][---newly allocated space---]
                // We move the block [0...tail-1] to the start of the new space to maintain continuity
                // After resize:  [moved now empty][head...oldSize-1][oldSize...tail-1][empty space]
                std::memcpy(m_buffer.data() + oldSize, m_buffer.data(), m_tail * sizeof(T));

                // Update tail to point to the next available slot in the new section
                m_tail = oldSize + m_tail;
            }
            // If tail > head, elements are already contiguous; no movement needed after resize.
        }

        m_buffer[m_tail] = v;
        m_tail = (m_tail + 1) == m_buffer.size() ? 0 : (m_tail + 1);
        m_count++;
    }

    void pop() {
        if (m_count > 0) {
            std::memset(&m_buffer[m_head], 0, sizeof(T));
            m_head = (m_head + 1) == m_buffer.size() ? 0 : (m_head + 1);
            m_count--;
        }
    }

    template <typename Predicate>
    void erase_if(Predicate predicate) {
        if (m_count == 0) return;

        size_t write_idx = 0;
        size_t buffer_size = m_buffer.size();

        for (size_t read_idx = 0; read_idx < m_count; ++read_idx) {
            size_t sum = m_head + read_idx;
            size_t current_pos = (sum >= buffer_size) ? (sum - buffer_size) : sum;

            // If the element does NOT satisfy the predicate, we keep it
            if (!predicate(m_buffer[current_pos])) {
                if (write_idx != read_idx) {
                    size_t sum_target = m_head + write_idx;
                    size_t target_pos = (sum_target >= buffer_size) ? (sum_target - buffer_size) : sum_target;

                    m_buffer[target_pos] = m_buffer[current_pos];
                }
                write_idx++;
            }
        }

        m_count = write_idx;
        size_t raw_tail = m_head + m_count;
        m_tail = (raw_tail >= buffer_size) ? raw_tail - buffer_size : raw_tail;
    }

    T& front() { return m_buffer[m_head]; }
    const T& front() const { return m_buffer[m_head]; }

    bool empty() const { return m_count == 0; }
    size_t size() const { return m_count; }
    size_t capacity() const { return m_buffer.size(); }

    class Iterator {
        VectorQueue* m_queue;
        size_t m_index;

       public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        Iterator(VectorQueue* q, size_t idx) : m_queue(q), m_index(idx) {}

        reference operator*() const {
            size_t pos = (m_queue->m_head + m_index) % m_queue->m_buffer.size();
            return m_queue->m_buffer[pos];
        }

        pointer operator->() const { return &(operator*()); }

        Iterator& operator++() {
            m_index++;
            return *this;
        }

        Iterator operator++(int) {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const Iterator& other) const { return m_index == other.m_index; }
        bool operator!=(const Iterator& other) const { return !(*this == other); }
    };

    Iterator begin() { return Iterator(this, 0); }
    Iterator end() { return Iterator(this, m_count); }

    Iterator begin() const { return Iterator(this, 0); }
    Iterator end() const { return Iterator(this, m_count); }
    Iterator cbegin() const { return Iterator(this, 0); }
    Iterator cend() const { return Iterator(this, m_count); }
};

}  // namespace LFI
