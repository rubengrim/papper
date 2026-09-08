#ifndef _PAPPER_QUEUE_H_
#define _PAPPER_QUEUE_H_

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <optional>
#include <utility>

namespace papper::queue
{

class Queue
{
    static constexpr size_t _cache_line_len
        = std::hardware_destructive_interference_size;

  public:
    Queue(const size_t capacity)
        // Use the closest power of two larger or equal to capacity for more
        // efficient index wrapping
        : _capacity{ std::bit_ceil(capacity) }, _wrap_mask{ _capacity - 1 },
          _end{ _capacity }
    {
        _buffer = new std::byte[_capacity];
    }

    ~Queue()
    {
        delete[] _buffer;
    }

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
            const size_t end = _end.load(std::memory_order_relaxed);
            if (read + size <= end)
            {
                _future_read_pos = (read + size) & _wrap_mask;
                return &_buffer[read];
            }
            else if (size <= write)
            {
                _end.store(_capacity, std::memory_order_relaxed);
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
    const size_t _capacity;
    const size_t _wrap_mask;

    std::byte* _buffer;
    alignas(_cache_line_len) std::atomic<size_t> _r{ 0 };
    alignas(_cache_line_len) std::atomic<size_t> _w{ 0 };

    // Set by the writer when it wraps so the reader knows not to read after
    // this point.
    // Fixes issue with the reader thinking the LogEventHeader lies at the
    // buffer tail just because it fits there, when in reality the writer
    // wrapped and wrote at the buffer beginning (because it writes header +
    // payload in one call, while reader reads header first and then payload)
    alignas(_cache_line_len) std::atomic<size_t> _end;

    // When memory is reserved, these indices are set to where _w/_r should be
    // set to after the write/read is commited
    // TODO: Put this logic in some RAII wrapper for safer and easier queue
    // access
    size_t _future_write_pos = 0;
    size_t _future_read_pos = 0;
};

}

#endif
