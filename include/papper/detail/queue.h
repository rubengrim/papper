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
    static constexpr size_t _alignment
        = std::hardware_destructive_interference_size;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

  public:
    Queue(const size_t capacity)
        // Use the closest power of two larger or equal to capacity for more
        // efficient index wrapping
        // TODO: Validate capacity somehow
        : _capacity{ std::bit_ceil(capacity) }, _wrap_mask{ _capacity - 1 }

    {
        // Allocate twice the capacity for more efficient end of buffer wrap
        // logic
        _buffer = new std::byte[2 * _capacity];
        memset(_buffer, 0, 2 * _capacity);
    }

    ~Queue()
    {
        delete[] _buffer;
    }

    struct WriteReservation
    {
        std::byte* buffer;
        const size_t new_write_pos;
    };

    WriteReservation reserve_write(const size_t size) noexcept
    {
        if (_cached_r + _capacity - _w < size)
        {
            // Not enough space, reload cached r and try again
            _cached_r = _atomic_r.load(std::memory_order_acquire);

            if (_cached_r + _capacity - _w < size)
            {
                return WriteReservation{ nullptr, 0 };
            }
        }

        return WriteReservation{ &_buffer[_w & _wrap_mask], _w + size };
    }

    void commit_write(const size_t new_write_pos) noexcept
    {
        _w = new_write_pos;
        _atomic_w.store(_w, std::memory_order_release);
    }

    struct ReadReservation
    {
        const std::byte* buffer;
        const size_t current_read_pos;
    };

    // Returns an address from where it is safe to read at most one log event
    const ReadReservation reserve_read() noexcept
    {
        // If not empty, we assume there is at least one full log event
        // to read
        if (is_empty())
        {
            return ReadReservation{ nullptr, 0 };
        }

        return ReadReservation{ &_buffer[_r & _wrap_mask], _r };
    }

    // When committing a read, call with ReadReservation::current_read_pos
    // plus the actual size that was read
    void commit_read(const size_t new_read_pos) noexcept
    {
        _r = new_read_pos;
        _atomic_r.store(_r, std::memory_order_release);
    }

    bool is_empty()
    {
        if (_cached_w <= _r)
        {
            // Reload cached w to really make sure it's empty
            _cached_w = _atomic_w.load(std::memory_order_acquire);
            return _cached_w == _r;
        }
        else
        {
            return false;
        }
    }

  private:
    const size_t _capacity;
    const size_t _wrap_mask;

    std::byte* _buffer;

    // Shared
    alignas(_alignment) std::atomic<size_t> _atomic_r = 0;
    alignas(_alignment) std::atomic<size_t> _atomic_w = 0;

    // Producer local
    alignas(_alignment) size_t _w = 0;
    size_t _cached_r = 0;

    // Consumer local
    alignas(_alignment) size_t _r = 0;
    size_t _cached_w = 0;
};

}
