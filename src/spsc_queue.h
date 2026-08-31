#ifndef _SPSC_QUEUE_H_
#define _SPSC_QUEUE_H_

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <new>
#include <optional>
#include <utility>

/**
* Wait-free and thread-safe spsc queue.
* True capacity is capacity-1 for implementation reasons.
*/
template <typename T, size_t capacity>
class SPSCQueue
{
    static_assert(capacity >= 2, "capacity must be >= 2");

    static constexpr size_t _capacity = std::bit_ceil(capacity);
    static constexpr size_t _wrap_mask = _capacity - 1;
    static constexpr size_t _cache_line_len
        = std::hardware_destructive_interference_size;

  public:
    bool push(const T& value)
    {
        const size_t write = _w.load(std::memory_order_relaxed);
        const size_t read = _r.load(std::memory_order_acquire);

        size_t write_next = (write + 1) & _wrap_mask;
        if (write_next == read)
            return false;

        _buffer[write] = value;

        _w.store(write_next, std::memory_order_release);

        return true;
    }

    bool push(T&& value)
    {
        const size_t write = _w.load(std::memory_order_relaxed);
        const size_t read = _r.load(std::memory_order_acquire);

        size_t write_next = (write + 1) & _wrap_mask;
        if (write_next == read)
            return false; // Queue is full

        _buffer[write] = std::move(value);

        _w.store(write_next, std::memory_order_release);

        return true;
    }

    bool pop(T& value)
    {
        const size_t write = _w.load(std::memory_order_acquire);
        size_t read = _r.load(std::memory_order_acquire);

        if (write == read)
            return false; // Queue is empty

        value = std::move(_buffer[read]);

        read = (read + 1) & _wrap_mask;
        _r.store(read, std::memory_order_release);

        return true;
    }

    std::optional<T> pop()
    {
        const size_t write = _w.load(std::memory_order_acquire);
        size_t read = _r.load(std::memory_order_relaxed);

        if (write == read)
            return {}; // Queue is empty

        T value = std::move(_buffer[read]);

        read = (read + 1) & _wrap_mask;
        _r.store(read, std::memory_order_release);

        return std::move(value);
    }

  private:
    T _buffer[_capacity];
    alignas(_cache_line_len) std::atomic<size_t> _r { 0 };
    alignas(_cache_line_len) std::atomic<size_t> _w { 0 };
};

#endif
