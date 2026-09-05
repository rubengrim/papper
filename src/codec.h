#ifndef _CODECS_H_
#define _CODECS_H_

#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

/*
 * Fallback for not supported types
 */
template <typename T>
struct Codec
{
    static size_t encoded_size(const T&)
    {
        static_assert(false, "type not supported");
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
        return sizeof(size_t) + value.size() * sizeof(CharT);
    }

    static void encode(std::byte*& buffer, const StringType& value)
    {
        const size_t length = value.length();
        const size_t byte_size = length * sizeof(CharT);

        std::memcpy(buffer, &length, sizeof(size_t));
        buffer += sizeof(size_t);
        std::memcpy(buffer, value.data(), byte_size);
        buffer += byte_size;
    }

    static void decode(const std::byte*& buffer, StringType& value)
    {
        size_t length;
        std::memcpy(&length, buffer, sizeof(size_t));
        buffer += sizeof(size_t);

        value.resize(length);

        const size_t byte_size = length * sizeof(CharT);
        std::memcpy(value.data(), buffer, byte_size);
        buffer += byte_size;
    }
};

#endif
