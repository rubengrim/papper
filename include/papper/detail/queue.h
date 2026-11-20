#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstring>
#include <new>

#include "globals.h"

namespace papper::detail
{

class Queue
{
  public:
    Queue(const size_t capacity)
        // Use the closest power of two larger or equal to capacity for more
        // efficient index wrapping
        // TODO: Validate capacity somehow
        : _capacity{ std::bit_ceil(capacity) },
          _physical_capacity{ 2 * _capacity }, _wrap_mask{ _capacity - 1 }

    {
        // Allocate twice the capacity (_physical_capacity) because events are
        // allowed to overlap _capacity
        // TODO: Could this throw?
        _buffer = static_cast<std::byte*>(::operator new(
            _physical_capacity, std::align_val_t{ globals::alignment }));
        memset(_buffer, 0, _physical_capacity);
    }

    ~Queue()
    {
        operator delete(_buffer, std::align_val_t{ globals::alignment });
    }

    Queue(Queue&) = delete;
    Queue& operator=(Queue&) = delete;

    size_t get_capacity() const noexcept
    {
        return _capacity;
    }

    size_t get_physical_capacity() const noexcept
    {
        return _physical_capacity;
    }

    // Can only be called by consumer
    size_t get_n_available_bytes() const noexcept
    {
        return _atomic_w.load(std::memory_order_acquire) - _r;
    }

    // Can only be called by consumer
    bool is_empty() noexcept
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

    struct SingleEventWriteReservation
    {
        std::byte* buffer;
        size_t new_write_pos;
        bool overflows;
    };

    SingleEventWriteReservation reserve_write(const size_t size) noexcept
    {
        if (_cached_r + _capacity - _w < size)
        {
            // Not enough space, reload cached r and try again
            _cached_r = _atomic_r.load(std::memory_order_acquire);

            if (_cached_r + _capacity - _w < size)
            {
                return SingleEventWriteReservation{ nullptr, 0, false };
            }
        }

        const size_t physical_write = _w & _wrap_mask;
        // Check if the write will spill over into the buffer padding area
        const bool overflows = physical_write + size >= _capacity;

        return SingleEventWriteReservation{ &_buffer[physical_write],
                                            _w + size,
                                            overflows };
    }

    void commit_write(const size_t new_write_pos,
                      const bool overflows) noexcept
    {
        _w = new_write_pos;

        // If this write overlapped into the buffer padding area, mark where
        // the overlap ended
        if (overflows)
        {
            _end_marker.store(_w, std::memory_order_relaxed);
        }

        _atomic_w.store(_w, std::memory_order_release);
    }

    struct SingleEventReadReservation
    {
        const std::byte* buffer;
        const size_t current_read_pos;
    };

    // Returns an address from where it is safe to read at most one log event
    const SingleEventReadReservation reserve_read() noexcept
    {
        // If not empty, we assume there is at least one full log event
        // to read
        if (is_empty())
        {
            return SingleEventReadReservation{ nullptr, 0 };
        }

        return SingleEventReadReservation{ &_buffer[_r & _wrap_mask], _r };
    }

    // Call with SingleEventReadReservation::current_read_pos
    // plus the actual size that was read
    void commit_single_event_read(const size_t new_read_pos) noexcept
    {
        _r = new_read_pos;
        _atomic_r.store(_r, std::memory_order_release);
    }

    struct MultipleEventReadReservation
    {
        const std::byte* buffer;
        // Allowed number of bytes to read from buffer
        size_t size;
        size_t new_read_pos;
    };

    MultipleEventReadReservation reserve_read_many_continuous() noexcept
    {
        const size_t read = _r;
        const size_t write = _atomic_w.load(std::memory_order_acquire);

        if (write <= read)
        {
            // Queue empty
            return MultipleEventReadReservation{ nullptr, 0, 0 };
        }

        const size_t physical_read = read & _wrap_mask;
        const size_t physical_write = write & _wrap_mask;
        if (physical_write <= physical_read) [[unlikely]]
        {
            // write has wrapped so we can read to the end of the physical
            // buffer (to _end_marker)
            const size_t end = _end_marker.load(std::memory_order_relaxed);
            return MultipleEventReadReservation{
                .buffer = &_buffer[read & _wrap_mask],
                .size = end - read,
                .new_read_pos = end,
            };
        }
        else
        {
            // Here we can only read up to write
            return MultipleEventReadReservation{ .buffer
                                                 = &_buffer[physical_read],
                                                 .size = write - read,
                                                 .new_read_pos = write };
        }
    }

    // Call with MultipleEventReadReservation::new_read_pos
    void commit_multiple_event_read(const size_t new_read_pos) noexcept
    {
        _r = new_read_pos;
        _atomic_r.store(_r, std::memory_order_release);
    }

  private:
    const size_t _capacity;
    const size_t _physical_capacity;
    const size_t _wrap_mask;

    std::byte* _buffer;

    // Shared
    alignas(globals::alignment) std::atomic<size_t> _atomic_r = 0;
    alignas(globals::alignment) std::atomic<size_t> _atomic_w = 0;
    alignas(globals::alignment) std::atomic<size_t> _end_marker = _capacity;

    // Producer local
    alignas(globals::alignment) size_t _w = 0;
    size_t _cached_r = 0;

    // Consumer local
    alignas(globals::alignment) size_t _r = 0;
    size_t _cached_w = 0;
};

}
