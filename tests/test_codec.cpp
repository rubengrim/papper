#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fmt/format.h"
#include "papper/detail/codec.h"
#include "papper/detail/core.h"

using papper::detail::Codec;
using papper::detail::decoded_t;

namespace
{

// String types decode into a view of the encode buffer, so they are copied
// into an owning std::string before the buffer is freed
template <typename T>
using owning_decoded_t
    = std::conditional_t<std::is_same_v<decoded_t<T>, std::string_view>,
                         std::string,
                         decoded_t<T>>;

template <typename T>
owning_decoded_t<T> roundtrip(const T& value)
{
    const size_t size = Codec<T>::encoded_size(value);
    std::vector<std::byte> storage(size);

    std::byte* write = storage.data();
    Codec<T>::encode(write, value, size);
    CHECK(write == storage.data() + storage.size());

    decoded_t<T> decoded{};
    const std::byte* read = storage.data();
    Codec<T>::decode(read, decoded);
    CHECK(read == storage.data() + storage.size());

    return owning_decoded_t<T>(decoded);
}

enum class Color : uint8_t
{
    Red,
    Green
};

struct Point
{
    int x;
    double y;
    bool operator==(const Point&) const = default;
};

} // namespace

// Opt Point in to the memcpy codec
template <>
inline constexpr bool papper::detail::enable_simple_codec<Point> = true;

TEST_CASE("codec: simple types roundtrip")
{
    CHECK(roundtrip(42) == 42);
    CHECK(roundtrip(-1LL) == -1LL);
    CHECK(roundtrip(uint8_t{ 255 }) == 255);
    CHECK(roundtrip(3.25) == 3.25);
    CHECK(roundtrip('x') == 'x');
    CHECK(roundtrip(true) == true);
    CHECK(roundtrip(Color::Green) == Color::Green);

    int target = 0;
    void* ptr = &target;
    CHECK(roundtrip(ptr) == ptr);

    CHECK(roundtrip(std::chrono::milliseconds{ 1500 })
          == std::chrono::milliseconds{ 1500 });
    const auto now = std::chrono::system_clock::now();
    CHECK(roundtrip(now) == now);
}

TEST_CASE("codec: types opted in with enable_simple_codec roundtrip")
{
    CHECK(roundtrip(Point{ 3, -1.5 }) == Point{ 3, -1.5 });
}

TEST_CASE("codec: C-strings roundtrip")
{
    const char* empty = "";
    CHECK(roundtrip(empty) == "");

    const char* hello = "hello";
    CHECK(roundtrip(hello) == "hello");

    char mutable_str[] = "mutable";
    char* mutable_ptr = mutable_str;
    CHECK(roundtrip(mutable_ptr) == "mutable");

    const std::string long_str(10'000, 'z');
    CHECK(roundtrip(long_str.c_str()) == long_str);
}

TEST_CASE("codec: nullptr C-string decodes as empty")
{
    const char* null_str = nullptr;
    CHECK(Codec<const char*>::encoded_size(null_str) == 1);
    CHECK(roundtrip(null_str) == "");
}

TEST_CASE("codec: C-string encoded size is length plus terminator")
{
    const char* str = "abc";
    CHECK(Codec<const char*>::encoded_size(str) == 4);
}

TEST_CASE("codec: decode_and_format decodes a sequence of mixed arguments")
{
    const int i = 7;
    const char* s = "seven";
    const double d = 7.5;

    const size_t i_size = Codec<int>::encoded_size(i);
    const size_t s_size = Codec<const char*>::encoded_size(s);
    const size_t d_size = Codec<double>::encoded_size(d);

    std::vector<std::byte> storage(i_size + s_size + d_size);
    std::byte* write = storage.data();
    Codec<int>::encode(write, i, i_size);
    Codec<const char*>::encode(write, s, s_size);
    Codec<double>::encode(write, d, d_size);

    const std::byte* read = storage.data();

    fmt::memory_buffer formatted;
    static constexpr auto fmt_str = FMT_COMPILE("{} is {} ({})");
    papper::detail::decode_and_format<std::remove_cvref_t<decltype(fmt_str)>,
                                      int,
                                      const char*,
                                      double>(formatted, read);
    CHECK(fmt::to_string(formatted) == "7 is seven (7.5)");
    CHECK(read == storage.data() + storage.size());
}
