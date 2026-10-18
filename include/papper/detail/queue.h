#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstring>
#include <new>

namespace papper::detail
{

class Queue
{
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winterference-size"
#endif
    // static constexpr size_t _cache_line_len = 128;
    static constexpr size_t _cache_line_len
        = std::hardware_destructive_interference_size;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

  public:
    Queue(const size_t capacity)
        // Use the closest power of two larger or equal to capacity for more
        // efficient index wrapping
        : _capacity{ std::bit_ceil(capacity) }, _end{ _capacity }
    {
        _buffer = new std::byte[_capacity];
        memset(_buffer, 0, _capacity);
    }

    ~Queue()
    {
        delete[] _buffer;
    }

    std::byte* reserve_write(const size_t size)
    {
        const size_t write = _w.load(std::memory_order_relaxed);

        auto try_reserve = [&](const size_t read) -> std::byte* {
            if (write >= read)
            {
                if (write + size <= _capacity)
                {
                    _future_write_pos = write + size;
                    return &_buffer[write];
                }
                else if (size < read)
                {
                    _end.store(write, std::memory_order_relaxed);
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
        };

        std::byte* buffer = try_reserve(_cached_r);
        if (buffer != nullptr)
            return buffer;

        // Reload _r and try again
        _cached_r = _r.load(std::memory_order_acquire);
        return try_reserve(_cached_r);
    }

    void commit_write()
    {
        _w.store(_future_write_pos, std::memory_order_release);
    }

    const std::byte* read_all_available(size_t& n_read)
    {
        const size_t read = _r.load(std::memory_order_relaxed);
        const size_t write = _w.load(std::memory_order_acquire);

        if (write > read)
        {
            n_read = write - read;
            _future_read_pos = read + n_read;
            return &_buffer[read];
        }
        else if (write == read)
        {
            n_read = 0;
            return nullptr;
        }
        else // read > write
        {
            const size_t end = _end.load(std::memory_order_relaxed);
            if (read < end)
            {
                // There are bytes to read before the end marker
                n_read = end - read;
                _future_read_pos = 0;
                _reset_end_on_commit = true;
                return &_buffer[read];
            }
            else if (write > 0) // Wrap to beginning and check there
            {
                // There are bytes to read from the beginning of the buffer
                n_read = write;
                _future_read_pos = write;
                _reset_end_on_commit = true;
                return &_buffer[0];
            }
            else
            {
                n_read = 0;
                return nullptr;
            }
        }
    }

    void commit_read()
    {
        if (_reset_end_on_commit)
        {
            _end.store(_capacity, std::memory_order_relaxed);
            _reset_end_on_commit = false;
        }
        _r.store(_future_read_pos, std::memory_order_release);
    }

    size_t get_capacity()
    {
        return _capacity;
    }

  private:
    const size_t _capacity;

    std::byte* _buffer;
    alignas(_cache_line_len) std::atomic<size_t> _r = 0;
    alignas(_cache_line_len) std::atomic<size_t> _w = 0;

    alignas(_cache_line_len) size_t _cached_r = 0;
    // When memory is reserved, this index is set to where _w should be
    // after the write is commited
    size_t _future_write_pos = 0;

    // When memory is reserved, this index is set to where _r should be
    // after the read is commited
    alignas(_cache_line_len) size_t _future_read_pos = 0;
    bool _reset_end_on_commit = false;

    // Set by the writer when it wraps so the reader knows to wrap there too
    alignas(_cache_line_len) std::atomic<size_t> _end;
};

}
