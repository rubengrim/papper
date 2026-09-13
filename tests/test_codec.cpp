#include <catch2/catch_test_macros.hpp>
#include <papper/detail/codec.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
struct SimplePoint
{
    int x;
    int y;
    double weight;

    bool operator==(const SimplePoint& other) const = default;
};
} // namespace

TEST_CASE("Codec trivially copyable types", "[codec]")
{
    using papper::detail::Codec;

    SECTION("Primitive types encoded_size matches sizeof")
    {
        int i = 42;
        REQUIRE(Codec<int>::encoded_size(i) == sizeof(int));

        double d = 3.14159;
        REQUIRE(Codec<double>::encoded_size(d) == sizeof(double));

        uint64_t u = 0xDEADBEEFCAFEBABEULL;
        REQUIRE(Codec<uint64_t>::encoded_size(u) == sizeof(uint64_t));

        SimplePoint pt{ 10, -20, 4.5 };
        REQUIRE(Codec<SimplePoint>::encoded_size(pt) == sizeof(SimplePoint));
    }

    SECTION("Encode and decode primitive values")
    {
        std::vector<std::byte> storage(128);
        std::byte* write_ptr = storage.data();

        int orig_i = 12345;
        double orig_d = 2.718281828;
        SimplePoint orig_pt{ 100, 200, 99.9 };

        Codec<int>::encode(write_ptr, orig_i);
        Codec<double>::encode(write_ptr, orig_d);
        Codec<SimplePoint>::encode(write_ptr, orig_pt);

        size_t bytes_written = write_ptr - storage.data();
        REQUIRE(bytes_written
                == sizeof(int) + sizeof(double) + sizeof(SimplePoint));

        const std::byte* read_ptr = storage.data();
        int dec_i = 0;
        double dec_d = 0.0;
        SimplePoint dec_pt{};

        Codec<int>::decode(read_ptr, dec_i);
        REQUIRE(dec_i == orig_i);

        Codec<double>::decode(read_ptr, dec_d);
        REQUIRE(dec_d == orig_d);

        Codec<SimplePoint>::decode(read_ptr, dec_pt);
        REQUIRE(dec_pt == orig_pt);

        REQUIRE(read_ptr == write_ptr);
    }
}

TEST_CASE("Codec std::string encoding and decoding", "[codec]")
{
    using papper::detail::Codec;

    SECTION("Empty string")
    {
        std::string empty = "";
        REQUIRE(Codec<std::string>::encoded_size(empty) == sizeof(size_t));

        std::vector<std::byte> storage(64);
        std::byte* write_ptr = storage.data();
        Codec<std::string>::encode(write_ptr, empty);
        REQUIRE(write_ptr - storage.data() == sizeof(size_t));

        const std::byte* read_ptr = storage.data();
        std::string decoded = "not empty";
        Codec<std::string>::decode(read_ptr, decoded);
        REQUIRE(decoded == "");
        REQUIRE(read_ptr == write_ptr);
    }

    SECTION("Non-empty string")
    {
        std::string text = "Hello, high performance async logger!";
        size_t expected_size = sizeof(size_t) + text.size();
        REQUIRE(Codec<std::string>::encoded_size(text) == expected_size);

        std::vector<std::byte> storage(128);
        std::byte* write_ptr = storage.data();
        Codec<std::string>::encode(write_ptr, text);
        REQUIRE(write_ptr - storage.data() == expected_size);

        const std::byte* read_ptr = storage.data();
        std::string decoded;
        Codec<std::string>::decode(read_ptr, decoded);
        REQUIRE(decoded == text);
        REQUIRE(read_ptr == write_ptr);
    }

    SECTION("Interleaved primitive types and strings")
    {
        std::string s1 = "alpha";
        int val = 999;
        std::string s2 = "beta-gamma-delta";

        std::vector<std::byte> storage(256);
        std::byte* write_ptr = storage.data();

        Codec<std::string>::encode(write_ptr, s1);
        Codec<int>::encode(write_ptr, val);
        Codec<std::string>::encode(write_ptr, s2);

        const std::byte* read_ptr = storage.data();
        std::string out_s1;
        int out_val = 0;
        std::string out_s2;

        Codec<std::string>::decode(read_ptr, out_s1);
        Codec<int>::decode(read_ptr, out_val);
        Codec<std::string>::decode(read_ptr, out_s2);

        REQUIRE(out_s1 == s1);
        REQUIRE(out_val == val);
        REQUIRE(out_s2 == s2);
        REQUIRE(read_ptr == write_ptr);
    }
}
