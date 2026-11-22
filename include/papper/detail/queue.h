#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "globals.h"
#include "platform.h"

#if defined(PAPPER_X86)
#include <immintrin.h>
#endif

namespace papper::detail
{

#if defined(PAPPER_X86)
__attribute__((target("clflushopt"))) inline void
flush_lines_clflushopt(std::uintptr_t first, const std::uintptr_t last,
                       const std::uintptr_t line_size) noexcept
{
    for (; first < last; first += line_size)
    {
        _mm_clflushopt(reinterpret_cast<void*>(first));
    }
}

inline void flush_lines_clflush(std::uintptr_t first,
                                const std::uintptr_t last,
                                const std::uintptr_t line_size) noexcept
{
    for (; first < last; first += line_size)
    {
        _mm_clflush(reinterpret_cast<void*>(first));
    }
}
#endif

// Flushes the cache lines from the one containing begin up to, but not
// including, the one containing end from all caches
inline void flush_cache_lines(const std::byte* begin,
                              const std::byte* end) noexcept
{
#if defined(PAPPER_X86)
    constexpr std::uintptr_t line_size = 64;
    const std::uintptr_t first
        = reinterpret_cast<std::uintptr_t>(begin) & ~(line_size - 1);
    const std::uintptr_t last
        = reinterpret_cast<std::uintptr_t>(end) & ~(line_size - 1);

    // clflushopt isn't ordered against other flushes, so it is much cheaper
    // than clflush when flushing many lines. clflush is always available on
    // x86-64
    static const bool has_clflushopt = [] {
        __builtin_cpu_init();
        return __builtin_cpu_supports("clflushopt") != 0;
    }();

    if (has_clflushopt)
    {
        flush_lines_clflushopt(first, last, line_size);
    }
    else
    {
        flush_lines_clflush(first, last, line_size);
    }
#else
    (void)begin;
    (void)end;
#endif
}

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
    };

    SingleEventWriteReservation reserve_write(const size_t size) noexcept
    {
        if (_cached_r + _capacity - _w < size)
        {
            // Not enough space, reload cached r and try again
            _cached_r = _atomic_r.load(std::memory_order_acquire);

            if (_cached_r + _capacity - _w < size)
            {
                return SingleEventWriteReservation{ nullptr, 0 };
            }
        }

        return SingleEventWriteReservation{ &_buffer[_w & _wrap_mask],
                                            _w + size };
    }

    void commit_write(const size_t new_write_pos) noexcept
    {
        // Compare the old write pos with the new one and determine if the
        // write overflows outside the capacity and into the buffer padding
        // area, which means the next write will wrap to the beginning of the
        // buffer
        // Mark where the overflow ends so the reader knows it
        if (((_w ^ new_write_pos) & ~_wrap_mask) != 0)
        {
            _end_marker.store(new_write_pos, std::memory_order_relaxed);
        }

        _w = new_write_pos;
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
        // Flush the cache lines that this read touched (except the last one)
        // This is done because in some cases these lines will stay in the
        // consumer thread's cache even after reading is finished, which can
        // make future writes to that memory slow
        // The last cache line of the read is not flushed because the writer
        // might still use part of it
        const std::byte* begin = &_buffer[_r & _wrap_mask];
        const std::byte* end = begin + (new_read_pos - _r);
        flush_cache_lines(begin, end);

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
