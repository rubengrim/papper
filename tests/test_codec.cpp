#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "papper/detail/codec.h"
#include "papper/detail/core.h"

using papper::detail::Codec;

namespace
{

template <typename T>
T roundtrip(const T& value)
{
    std::vector<std::byte> storage(Codec<T>::encoded_size(value));

    std::byte* write = storage.data();
    Codec<T>::encode(write, value);
    CHECK(write == storage.data() + storage.size());

    T decoded{};
    const std::byte* read = storage.data();
    Codec<T>::decode(read, decoded);
    CHECK(read == storage.data() + storage.size());

    return decoded;
}

struct Point
{
    int x;
    double y;
    bool operator==(const Point&) const = default;
};

} // namespace

TEST_CASE("codec: trivially copyable types roundtrip")
{
    CHECK(roundtrip(42) == 42);
    CHECK(roundtrip(-1LL) == -1LL);
    CHECK(roundtrip(uint8_t{ 255 }) == 255);
    CHECK(roundtrip(3.25) == 3.25);
    CHECK(roundtrip('x') == 'x');
    CHECK(roundtrip(true) == true);
    CHECK(roundtrip(Point{ 3, -1.5 }) == Point{ 3, -1.5 });
}

TEST_CASE("codec: strings roundtrip")
{
    CHECK(roundtrip(std::string{}) == "");
    CHECK(roundtrip(std::string{ "hello" }) == "hello");

    const std::string with_nul("a\0b", 3);
    CHECK(roundtrip(with_nul) == with_nul);

    const std::string long_str(10'000, 'z');
    CHECK(roundtrip(long_str) == long_str);
}

TEST_CASE("codec: string encoded size is length prefix plus characters")
{
    CHECK(Codec<std::string>::encoded_size("abc") == sizeof(size_t) + 3);
    CHECK(Codec<std::wstring>::encoded_size(L"abc")
          == sizeof(size_t) + 3 * sizeof(wchar_t));
    CHECK(roundtrip(std::wstring{ L"wide" }) == L"wide");
}

TEST_CASE("codec: decode_and_format decodes a sequence of mixed arguments")
{
    const int i = 7;
    const std::string s = "seven";
    const double d = 7.5;

    std::vector<std::byte> storage(Codec<int>::encoded_size(i)
                                   + Codec<std::string>::encoded_size(s)
                                   + Codec<double>::encoded_size(d));
    std::byte* write = storage.data();
    Codec<int>::encode(write, i);
    Codec<std::string>::encode(write, s);
    Codec<double>::encode(write, d);

    const std::byte* read = storage.data();
    const std::string formatted
        = papper::detail::decode_and_format<int, std::string, double>(
            "{} is {} ({})", read);
    CHECK(formatted == "7 is seven (7.5)");
}
