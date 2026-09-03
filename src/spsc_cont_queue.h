#ifndef _SPSC_CONT_QUEUE_H_
#define _SPSC_CONT_QUEUE_H_

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <optional>
#include <utility>

// Largely taken from:
// https://github.com/DNedic/lockfree/blob/main/lockfree/spsc/ring_buf.hpp
template <size_t capacity>
class SPSCContQueue
{
    static_assert(capacity >= 2, "capacity must be >= 2");

    static constexpr size_t _capacity = std::bit_ceil(capacity);
    static constexpr size_t _wrap_mask = _capacity - 1;
    static constexpr size_t _cache_line_len
        = std::hardware_destructive_interference_size;

  private:
    size_t free_to_write(const size_t read, const size_t write) const
    {
        if (read > write)
            return (read - write) - 1;
        else
            return (_capacity - (write - read)) - 1;
    }

    size_t available_to_read(const size_t read, const size_t write) const
    {
        if (write >= read)
            return write - read;
        else
            return _capacity - (read - write);
    }

  public:
    bool push(const std::byte* data, const size_t size)
    {
        size_t write = _w.load(std::memory_order_relaxed);
        const size_t read = _r.load(std::memory_order_acquire);

        if (size > free_to_write(read, write))
            return false; // Data doesn't fit, drop the write

        if (write + size <= _capacity)
        {
            // Data can be copied without wrapping
            std::memcpy(_buffer + write, data, size);
            write = (write + 1) & _wrap_mask;
        }
        else
        {
            // Copy until the end of _buffer, then wrap to beginning and copy
            // remaining
            const size_t to_end_of_buffer = _capacity - write;
            std::memcpy(_buffer + write, data, to_end_of_buffer);
            const size_t remaining = size - to_end_of_buffer;
            std::memcpy(_buffer, data + to_end_of_buffer, remaining);
            write = remaining;
        }

        _w.store(write, std::memory_order_release);

        return true;
    }

    bool pop(std::byte* data, const size_t size)
    {
        const size_t write = _w.load(std::memory_order_acquire);
        size_t read = _r.load(std::memory_order_relaxed);

        if (size > available_to_read(read, write))
            return false; // Not enough to read

        if (read + size <= _capacity)
        {
            // Data does not wrap so read linearly
            std::memcpy(data, _buffer + read, size);
            read = (read + 1) & _wrap_mask;
        }
        else
        {
            // Read until the end of _buffer, then wrap to beginning and read
            // remaining
            const size_t to_end_of_buffer = _capacity - read;
            std::memcpy(data, _buffer + read, to_end_of_buffer);
            const size_t remaining = size - to_end_of_buffer;
            std::memcpy(data + to_end_of_buffer, _buffer, remaining);
            read = remaining;
        }

        _r.store(read, std::memory_order_release);

        return true;
    }

    // Read (memcpy) directly into T
    template <typename T>
    bool pop(T& value)
    {
        const size_t size = sizeof(T);
        const size_t write = _w.load(std::memory_order_acquire);
        size_t read = _r.load(std::memory_order_relaxed);

        if (size > available_to_read(read, write))
            return false; // Not enough to read

        if (read + size <= _capacity)
        {
            // Data does not wrap so read linearly
            std::memcpy(&value, _buffer + read, size);
            read = (read + 1) & _wrap_mask;
        }
        else
        {
            // Read until the end of _buffer, then wrap to beginning and read
            // remaining
            const size_t to_end_of_buffer = _capacity - read;
            std::memcpy(value, _buffer + read, to_end_of_buffer);
            const size_t remaining = size - to_end_of_buffer;
            // reinterpret_cast to byte buffer so that pointer arithmetics
            // operate on byte level
            std::memcpy(reinterpret_cast<std::byte*>(&value)
                            + to_end_of_buffer,
                        _buffer,
                        remaining);
            read = remaining;
        }

        _r.store(read, std::memory_order_release);

        return true;
    }

  private:
    std::byte _buffer[_capacity];
    alignas(_cache_line_len) std::atomic<size_t> _r{ 0 };
    alignas(_cache_line_len) std::atomic<size_t> _w{ 0 };
};

#endif
