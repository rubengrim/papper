#ifndef _QUEUE2_H_
#define _QUEUE2_H_

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <optional>
#include <utility>

template <size_t min_capacity>
class SPSCQueue
{
    static_assert(min_capacity >= 2, "min_capacity must be >= 2");

    // Use the closest power of two larger or equal to min_capacity as the
    // queue capacity
    static constexpr size_t _capacity = std::bit_ceil(min_capacity);
    static constexpr size_t _wrap_mask = _capacity - 1;
    static constexpr size_t _cache_line_len
        = std::hardware_destructive_interference_size;

  public:
    std::byte* reserve_write(const size_t size)
    {
        const size_t write = _w.load(std::memory_order_relaxed);
        const size_t read = _r.load(std::memory_order_acquire);

        if (write >= read)
        {
            if (write + size <= _capacity)
            {
                _future_write_pos = write + size;
                return &_buffer[write];
            }
            else if (size < read)
            {
                _future_write_pos = size;
                return _buffer;
            }
            else
            {
                return nullptr;
            }
        }
        else // read > write
        {
            if (write + size <= read - 1)
            {
                _future_write_pos = write + size;
                return _buffer + write;
            }
            else
            {
                return nullptr;
            }
        }
    }

    void commit_write()
    {
        _w.store(_future_write_pos, std::memory_order_release);
    }

    const std::byte* reserve_read(const size_t size)
    {
        const size_t write = _w.load(std::memory_order_acquire);
        const size_t read = _r.load(std::memory_order_relaxed);

        if (write > read)
        {
            if (read + size <= write)
            {
                _future_read_pos = read + size;
                return &_buffer[read];
            }
            else
            {
                return nullptr;
            }
        }
        else if (read == write)
        {
            return nullptr;
        }
        else // read > write
        {
            if (read + size <= _capacity)
            {
                _future_read_pos = (read + size) & _wrap_mask;
                return &_buffer[read];
            }
            else if (size <= write)
            {
                _future_read_pos = size;
                return &_buffer[0];
            }
            else
            {
                return nullptr;
            }
        }
    }

    void commit_read()
    {
        _r.store(_future_read_pos, std::memory_order_release);
    }

  private:
    std::byte _buffer[_capacity];
    alignas(_cache_line_len) std::atomic<size_t> _r{ 0 };
    alignas(_cache_line_len) std::atomic<size_t> _w{ 0 };

    size_t _future_write_pos = 0;
    size_t _future_read_pos = 0;
};

#endif
