#ifndef _SERIALIZE_H_
#define _SERIALIZE_H_

#include <cstring>
#include <string>

inline constexpr size_t arg_buf_size = 121;

template <typename T>
void serialize_non_trivially_copyable(std::byte*, size_t&, const T&, bool&)
{
    static_assert(false, "type not supported");
}

/*
 * std::string
 */
template <>
void serialize_non_trivially_copyable(std::byte* buffer, size_t& offset,
                                      const std::string& arg, bool& too_big)
{
}

template <typename T>
void serialize_arg(std::byte* buffer, size_t& offset, const T& arg,
                   bool& too_big)
{
    if constexpr (std::is_trivially_copyable_v<T>)
    {
        if (offset + sizeof(T) >= arg_buf_size)
        {
            too_big = true;
            return;
        }

        std::memcpy(buffer + offset, &arg, sizeof(T));
        offset += sizeof(T);
    }
    else
    {
        serialize_non_trivially_copyable<T>(buffer, offset, arg, too_big);
    }
}

#endif
