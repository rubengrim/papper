#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace papper::detail
{

/*
 * Fallback for not supported types
 */
template <typename T>
struct Codec
{
    static size_t encoded_size(const T&)
    {
        static_assert(false, "type not supported");
        return 0;
    }

    static void encode(std::byte*&, const T&)
    {
        static_assert(false, "type not supported");
    }

    static void decode(const std::byte*&, T&)
    {
        static_assert(false, "type not supported");
    }
};

/*
 * Trivially copyable types
 */
template <typename T>
    requires std::is_trivially_copyable_v<T>
struct Codec<T>
{
    static size_t encoded_size(const T&)
    {
        return sizeof(T);
    }

    static void encode(std::byte*& buffer, const T& value)
    {
        std::memcpy(buffer, &value, sizeof(T));
        buffer += sizeof(T);
    }

    static void decode(const std::byte*& buffer, T& value)
    {
        std::memcpy(&value, buffer, sizeof(T));
        buffer += sizeof(T);
    }
};

/*
 * std::basic_string
 */
template <typename CharT, typename Traits, typename Alloc>
struct Codec<std::basic_string<CharT, Traits, Alloc>>
{
    using StringType = std::basic_string<CharT, Traits, Alloc>;

    static size_t encoded_size(const StringType& value)
    {
        return sizeof(uint32_t) + value.size() * sizeof(CharT);
    }

    static void encode(std::byte*& buffer, const StringType& value)
    {
        const uint32_t length = value.length();
        const uint32_t byte_size = length * sizeof(CharT);

        std::memcpy(buffer, &length, sizeof(uint32_t));
        buffer += sizeof(uint32_t);
        std::memcpy(buffer, value.data(), byte_size);
        buffer += byte_size;
    }

    static void decode(const std::byte*& buffer, StringType& value)
    {
        size_t length;
        std::memcpy(&length, buffer, sizeof(uint32_t));
        buffer += sizeof(uint32_t);

        value.resize(length);

        const uint32_t byte_size = length * sizeof(CharT);
        std::memcpy(value.data(), buffer, byte_size);
        buffer += byte_size;
    }
};

}
