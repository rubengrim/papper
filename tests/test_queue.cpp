#include <catch2/catch_test_macros.hpp>
#include <papper/detail/queue.h>

#include <cstddef>
#include <cstring>
#include <vector>

TEST_CASE("Queue basic write and read", "[queue]")
{
    papper::detail::Queue q(1024);

    SECTION("Empty queue returns nullptr on reserve_read")
    {
        REQUIRE(q.reserve_read(sizeof(int)) == nullptr);
    }

    SECTION("Single write and read")
    {
        std::byte* write_buf = q.reserve_write(sizeof(int));
        REQUIRE(write_buf != nullptr);

        int value = 42;
        std::memcpy(write_buf, &value, sizeof(int));
        q.commit_write();

        const std::byte* read_buf = q.reserve_read(sizeof(int));
        REQUIRE(read_buf != nullptr);

        int read_value = 0;
        std::memcpy(&read_value, read_buf, sizeof(int));
        REQUIRE(read_value == 42);

        q.commit_read();

        // After committing read, queue should be empty again
        REQUIRE(q.reserve_read(sizeof(int)) == nullptr);
    }

    SECTION("FIFO ordering with multiple elements")
    {
        constexpr int count = 50;
        for (int i = 0; i < count; ++i)
        {
            std::byte* w = q.reserve_write(sizeof(int));
            REQUIRE(w != nullptr);
            std::memcpy(w, &i, sizeof(int));
            q.commit_write();
        }

        for (int i = 0; i < count; ++i)
        {
            const std::byte* r = q.reserve_read(sizeof(int));
            REQUIRE(r != nullptr);
            int val = -1;
            std::memcpy(&val, r, sizeof(int));
            REQUIRE(val == i);
            q.commit_read();
        }

        REQUIRE(q.reserve_read(sizeof(int)) == nullptr);
    }
}

TEST_CASE("Queue capacity exhaustion", "[queue]")
{
    // Capacity 64 bytes (bit_ceil(64) = 64)
    papper::detail::Queue q(64);

    // Reserving more than capacity fails
    REQUIRE(q.reserve_write(128) == nullptr);

    // Fill the queue
    std::byte* w1 = q.reserve_write(40);
    REQUIRE(w1 != nullptr);
    q.commit_write();

    // Remaining space is < 40 bytes, so reserving 40 should return nullptr
    std::byte* w2 = q.reserve_write(40);
    REQUIRE(w2 == nullptr);

    // Consume the 40 bytes
    const std::byte* r1 = q.reserve_read(40);
    REQUIRE(r1 != nullptr);
    q.commit_read();

    // Now space is available again:
    // Fits in tail remaining space (64 - 40 = 24 bytes available at tail)
    std::byte* w3 = q.reserve_write(20);
    REQUIRE(w3 != nullptr);
    q.commit_write();

    // With _w at 60 and _r at 40, reserving 30 bytes cannot fit at tail (60 +
    // 30 > 64), but fits wrapped at the start since 30 < 40 (_r)
    std::byte* w4 = q.reserve_write(30);
    REQUIRE(w4 != nullptr);
    q.commit_write();
}

TEST_CASE("Queue buffer wrap-around behavior", "[queue]")
{
    // Queue with small capacity of 64 bytes
    papper::detail::Queue q(64);

    // Each element is 16 bytes.
    // 64 / 16 = 4 elements fit without wrapping.
    // We do write-and-read cycles to force write and read pointers to wrap
    // around.
    constexpr size_t chunk_size = 16;
    constexpr int total_iterations = 200;

    for (int i = 0; i < total_iterations; ++i)
    {
        std::byte* w = q.reserve_write(chunk_size);
        REQUIRE(w != nullptr);

        // Store pattern
        int payload[4] = { i, i + 1, i + 2, i + 3 };
        std::memcpy(w, payload, chunk_size);
        q.commit_write();

        const std::byte* r = q.reserve_read(chunk_size);
        REQUIRE(r != nullptr);

        int read_payload[4] = { 0 };
        std::memcpy(read_payload, r, chunk_size);
        REQUIRE(read_payload[0] == i);
        REQUIRE(read_payload[1] == i + 1);
        REQUIRE(read_payload[2] == i + 2);
        REQUIRE(read_payload[3] == i + 3);
        q.commit_read();
    }
}

TEST_CASE("Queue wrap-around when data wraps to beginning", "[queue]")
{
    // Capacity 64
    papper::detail::Queue q(64);

    // Write 48 bytes (pos 0 -> 48)
    std::byte* w1 = q.reserve_write(48);
    REQUIRE(w1 != nullptr);
    int marker1 = 1111;
    std::memcpy(w1, &marker1, sizeof(int));
    q.commit_write();

    // Read 48 bytes (read pos becomes 48)
    const std::byte* r1 = q.reserve_read(48);
    REQUIRE(r1 != nullptr);
    int res1 = 0;
    std::memcpy(&res1, r1, sizeof(int));
    REQUIRE(res1 == marker1);
    q.commit_read();

    // Now write 32 bytes:
    // Write pos is 48. 48 + 32 = 80 > 64, so it cannot fit at the tail!
    // But 32 < read pos (48), so it wraps to beginning (_buffer[0]).
    std::byte* w2 = q.reserve_write(32);
    REQUIRE(w2 != nullptr);
    int marker2 = 2222;
    std::memcpy(w2, &marker2, sizeof(int));
    q.commit_write();

    // Now reader is at 48.
    // Reader attempts reserve_read(32). Read pos 48 + 32 > _end (48).
    // So reader wraps to _buffer[0].
    const std::byte* r2 = q.reserve_read(32);
    REQUIRE(r2 != nullptr);
    int res2 = 0;
    std::memcpy(&res2, r2, sizeof(int));
    REQUIRE(res2 == marker2);
    q.commit_read();
}
