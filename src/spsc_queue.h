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
template <size_t min_capacity>
class SPSCQueue
{
    static_assert(min_capacity >= 2, "min_capacity must be >= 2");

    // Use the closest power of two larger or equal to min_capacity as the
    // queue capacity to allow for efficient index wrapping
    static constexpr size_t _capacity = std::bit_ceil(min_capacity);
    static constexpr size_t _wrap_mask = _capacity - 1;
    static constexpr size_t _cache_line_len
        = std::hardware_destructive_interference_size;

  public:
    bool push(const std::byte* data, const size_t size);

    bool pop(std::byte* data, const size_t size);

    // Read directly into T
    template <typename T>
    bool pop(T& value);

  private:
    size_t free_to_write(const size_t read, const size_t write) const;

    size_t available_to_read(const size_t read, const size_t write) const;

  private:
    std::byte _buffer[_capacity];
    alignas(_cache_line_len) std::atomic<size_t> _r{ 0 };
    alignas(_cache_line_len) std::atomic<size_t> _w{ 0 };
};

#endif
