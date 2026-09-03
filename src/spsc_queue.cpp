#include "spsc_queue.h"

template <size_t min_capacity>
bool SPSCQueue<min_capacity>::push(const std::byte* data, const size_t size)
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

template <size_t min_capacity>
bool SPSCQueue<min_capacity>::pop(std::byte* data, const size_t size)
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

template <size_t min_capacity>
template <typename T>
bool SPSCQueue<min_capacity>::pop(T& value)
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
        std::memcpy(reinterpret_cast<std::byte*>(&value) + to_end_of_buffer,
                    _buffer,
                    remaining);
        read = remaining;
    }

    _r.store(read, std::memory_order_release);

    return true;
}

template <size_t min_capacity>
size_t SPSCQueue<min_capacity>::free_to_write(const size_t read,
                                              const size_t write) const
{
    if (read > write)
        return (read - write) - 1;
    else
        return (_capacity - (write - read)) - 1;
}

template <size_t min_capacity>
size_t SPSCQueue<min_capacity>::available_to_read(const size_t read,
                                                  const size_t write) const
{
    if (read > write)
        return (read - write) - 1;
    else
        return (_capacity - (write - read)) - 1;
}
