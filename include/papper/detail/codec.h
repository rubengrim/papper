#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace papper::detail
{

inline size_t get_str_len(const char* str)
{
    if (str == nullptr)
    {
        return 0;
    }

    return std::strlen(str);
}

inline size_t get_str_len_bounded(const char* str, const uint32_t max_length)
{
    if (str == nullptr)
    {
        return 0;
    }

    const char* end = (const char*)std::memchr(str, '\0', max_length);

    if (end == nullptr)
    {
        return max_length;
    }
    else
    {
        return (size_t)(end - str);
    }

    return 0;
}

template <typename T>
inline constexpr bool is_duration_v = false;
template <typename R, typename P>
inline constexpr bool is_duration_v<std::chrono::duration<R, P>> = true;

template <typename T>
inline constexpr bool is_time_point_v = false;
template <typename C, typename D>
inline constexpr bool is_time_point_v<std::chrono::time_point<C, D>> = true;

template <typename T>
inline constexpr bool enable_simple_codec = false;

// Arithmetics, enums, void pointers, and chrono durations/time points can be
// encoded by a simple memcpy
// Any additional types that should take this path can specialize
// enable_simple_codec() to return true
template <typename T>
concept CanUseSimpleEncoding
    = (std::is_arithmetic_v<T> || std::is_enum_v<T> || std::same_as<T, void*>
       || std::same_as<T, const void*> || is_duration_v<T>
       || is_time_point_v<T> || enable_simple_codec<T>)
      && std::is_trivially_copyable_v<T>;

/*
 * Base template for Codec that handles every type satisfying
 * CanUseSimpleEncoding
 */
template <typename ArgT>
struct Codec
{
    static_assert(CanUseSimpleEncoding<ArgT>,
                  "papper does not define a codec for this type");
    using DecodedT = ArgT;

    static size_t encoded_size(const ArgT&)
    {
        return sizeof(ArgT);
    }

    static void encode(std::byte*& buffer, const ArgT& arg, const size_t size)
    {
        std::memcpy(buffer, &arg, size);
        buffer += size;
    }

    static void decode(const std::byte*& buffer, DecodedT& arg)
    {
        std::memcpy(&arg, buffer, sizeof(ArgT));
        buffer += sizeof(ArgT);
    }
};

/*
 * C-string
 */
template <typename ArgT>
    requires std::is_same_v<ArgT, const char*> || std::is_same_v<ArgT, char*>
struct Codec<ArgT>
{
    using DecodedT = std::string_view;

    static size_t encoded_size(const ArgT& arg)
    {
        return get_str_len(arg) + 1;
    }

    static void encode(std::byte*& buffer, const ArgT& arg, const size_t size)
    {
        // If not nullptr, copy the string content excluding end marker
        if (arg != nullptr)
        {
            std::memcpy(buffer, arg, size - 1);
        }

        // In all cases, even when nullptr, append \0
        buffer[size - 1] = (std::byte)'\0';

        buffer += size;
    }

    static void decode(const std::byte*& buffer, DecodedT& arg)
    {
        const char* str = reinterpret_cast<const char*>(buffer);
        const size_t len = get_str_len(str) + 1;
        buffer += len;
        arg = std::string_view{ str, len - 1 };
    }
};
}
