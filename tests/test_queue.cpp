#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <random>
#include <thread>
#include <vector>

#include "papper/detail/queue.h"

using papper::detail::Queue;

namespace
{

std::vector<std::byte> make_bytes(size_t size, uint8_t seed)
{
    std::vector<std::byte> bytes(size);
    for (size_t i = 0; i < size; ++i)
        bytes[i] = std::byte(uint8_t(seed + i * 31));
    return bytes;
}

bool write_bytes(Queue& q, const std::vector<std::byte>& bytes)
{
    std::byte* buffer = q.reserve_write(bytes.size());
    if (buffer == nullptr)
        return false;
    std::memcpy(buffer, bytes.data(), bytes.size());
    q.commit_write();
    return true;
}

std::optional<std::vector<std::byte>> read_bytes(Queue& q, size_t size)
{
    const std::byte* buffer = q.reserve_read(size);
    if (buffer == nullptr)
        return std::nullopt;
    std::vector<std::byte> bytes(buffer, buffer + size);
    q.commit_read();
    return bytes;
}

} // namespace

TEST_CASE("queue: empty queue has nothing to read")
{
    Queue q{ 64 };
    CHECK(q.reserve_read(1) == nullptr);
    CHECK(q.reserve_read(64) == nullptr);
}

TEST_CASE("queue: single write is read back unchanged")
{
    Queue q{ 64 };
    const auto msg = make_bytes(24, 1);

    REQUIRE(write_bytes(q, msg));
    CHECK(read_bytes(q, msg.size()) == msg);
    CHECK(q.reserve_read(1) == nullptr);
}

TEST_CASE("queue: messages are read in FIFO order")
{
    Queue q{ 256 };
    std::vector<std::vector<std::byte>> msgs;
    for (uint8_t i = 0; i < 10; ++i)
    {
        msgs.push_back(make_bytes(5 + i, i));
        REQUIRE(write_bytes(q, msgs.back()));
    }

    for (const auto& msg : msgs)
        CHECK(read_bytes(q, msg.size()) == msg);
    CHECK(q.reserve_read(1) == nullptr);
}

TEST_CASE("queue: uncommitted write is not visible to reader")
{
    Queue q{ 64 };
    std::byte* buffer = q.reserve_write(8);
    REQUIRE(buffer != nullptr);
    CHECK(q.reserve_read(8) == nullptr);

    q.commit_write();
    CHECK(q.reserve_read(8) != nullptr);
}

TEST_CASE("queue: uncommitted read does not free space")
{
    Queue q{ 64 };
    REQUIRE(write_bytes(q, make_bytes(64, 0)));

    REQUIRE(q.reserve_read(64) != nullptr);
    CHECK(q.reserve_write(1) == nullptr);

    q.commit_read();
    CHECK(q.reserve_write(1) != nullptr);
}

TEST_CASE("queue: capacity is rounded up to a power of two")
{
    Queue q{ 100 }; // -> 128

    const auto msg = make_bytes(128, 7);
    REQUIRE(write_bytes(q, msg));
    CHECK(q.reserve_write(1) == nullptr);
    CHECK(read_bytes(q, 128) == msg);
}

TEST_CASE("queue: reservation larger than capacity fails")
{
    Queue q{ 64 };
    CHECK(q.reserve_write(65) == nullptr);
    CHECK(q.reserve_write(1000) == nullptr);

    // Queue is still usable afterwards
    const auto msg = make_bytes(64, 3);
    REQUIRE(write_bytes(q, msg));
    CHECK(read_bytes(q, 64) == msg);
}

TEST_CASE("queue: full queue rejects writes until reader frees space")
{
    Queue q{ 64 };
    const auto a = make_bytes(32, 1);
    const auto b = make_bytes(32, 2);
    REQUIRE(write_bytes(q, a));
    REQUIRE(write_bytes(q, b));

    CHECK(q.reserve_write(1) == nullptr);

    CHECK(read_bytes(q, 32) == a);

    // Space at the front has been freed, so a write wraps around
    const auto c = make_bytes(16, 3);
    REQUIRE(write_bytes(q, c));

    CHECK(read_bytes(q, 32) == b);
    CHECK(read_bytes(q, 16) == c);
    CHECK(q.reserve_read(1) == nullptr);
}

TEST_CASE("queue: reading more than is available fails without consuming")
{
    Queue q{ 64 };
    const auto msg = make_bytes(10, 5);
    REQUIRE(write_bytes(q, msg));

    CHECK(q.reserve_read(11) == nullptr);
    CHECK(read_bytes(q, 10) == msg);
}

TEST_CASE("queue: write that doesn't fit at the tail wraps to the front")
{
    Queue q{ 64 };
    REQUIRE(write_bytes(q, make_bytes(40, 0)));
    REQUIRE(read_bytes(q, 40));

    // 40 + 30 > 64, so this has to be placed at the start of the buffer
    const auto msg = make_bytes(30, 9);
    REQUIRE(write_bytes(q, msg));
    CHECK(read_bytes(q, 30) == msg);
    CHECK(q.reserve_read(1) == nullptr);
}

TEST_CASE("queue: wrapping write needs strictly more room than its size")
{
    Queue q{ 64 };
    REQUIRE(write_bytes(q, make_bytes(40, 0)));
    REQUIRE(read_bytes(q, 40));

    // Read index is at 40. Wrapping a 40 byte write would make the write index
    // catch up with the read index, which would look like an empty queue
    CHECK(q.reserve_write(40) == nullptr);
    CHECK(q.reserve_write(39) != nullptr);
}

TEST_CASE("queue: two-phase read of a wrapped message reads from the front")
{
    // Mirrors how the backend reads: first a fixed size header, then the
    // payload, while the writer wrote both in a single reservation. The header
    // would fit in the unused tail, but the reader must not read it from there
    Queue q{ 64 };
    REQUIRE(write_bytes(q, make_bytes(40, 0)));
    REQUIRE(read_bytes(q, 40));

    const auto msg = make_bytes(30, 42); // 8 byte "header" + 22 byte payload
    REQUIRE(write_bytes(q, msg));

    const auto header = read_bytes(q, 8);
    const auto payload = read_bytes(q, 22);
    REQUIRE(header);
    REQUIRE(payload);
    CHECK(std::equal(header->begin(), header->end(), msg.begin()));
    CHECK(std::equal(payload->begin(), payload->end(), msg.begin() + 8));
    CHECK(q.reserve_read(1) == nullptr);
}

TEST_CASE("queue: writer behind a wrapped reader leaves a one byte gap")
{
    Queue q{ 64 };
    const auto a = make_bytes(40, 1);
    const auto b = make_bytes(24, 2);
    const auto c = make_bytes(10, 3);
    const auto d = make_bytes(9, 4);

    REQUIRE(write_bytes(q, a));
    const auto a_head = read_bytes(q, 20); // read index = 20
    REQUIRE(a_head);

    REQUIRE(write_bytes(q, b)); // fills tail exactly, write index = 64
    REQUIRE(write_bytes(q, c)); // wraps, write index = 10
    REQUIRE(write_bytes(q, d)); // write index = 19, one byte before read
    CHECK(q.reserve_write(1) == nullptr);

    CHECK(read_bytes(q, 20) == std::vector<std::byte>(a.begin() + 20, a.end()));
    CHECK(read_bytes(q, 24) == b);
    CHECK(read_bytes(q, 10) == c);
    CHECK(read_bytes(q, 9) == d);
    CHECK(q.reserve_read(1) == nullptr);
}

TEST_CASE("queue: randomized single threaded writes and reads match a model")
{
    Queue q{ 256 };
    std::deque<std::vector<std::byte>> model;
    std::mt19937 rng{ 1234 };
    std::uniform_int_distribution<size_t> size_dist{ 1, 100 };
    std::bernoulli_distribution do_write{ 0.55 };

    size_t n_written = 0;
    for (int i = 0; i < 100'000; ++i)
    {
        if (do_write(rng))
        {
            const auto msg = make_bytes(size_dist(rng), uint8_t(i));
            const bool ok = write_bytes(q, msg);
            if (model.empty() && msg.size() <= 128)
            {
                // An empty queue must always accept up to half its capacity
                REQUIRE(ok);
            }
            if (ok)
            {
                model.push_back(msg);
                ++n_written;
            }
        }
        else if (model.empty())
        {
            REQUIRE(q.reserve_read(1) == nullptr);
        }
        else
        {
            // Everything committed must be readable
            const auto msg = read_bytes(q, model.front().size());
            REQUIRE(msg);
            REQUIRE(*msg == model.front());
            model.pop_front();
        }
    }

    while (!model.empty())
    {
        REQUIRE(read_bytes(q, model.front().size()) == model.front());
        model.pop_front();
    }
    CHECK(q.reserve_read(1) == nullptr);
    CHECK(n_written > 10'000);
}

TEST_CASE("queue: concurrent producer and consumer (SPSC stress)")
{
    struct Header
    {
        uint64_t seq;
        uint32_t payload_size;
    };

    constexpr uint64_t n_messages = 300'000;
    constexpr uint32_t max_payload = 200;
    Queue q{ 4096 }; // Small to force lots of wrapping and full-queue cases

    auto payload_byte = [](uint64_t seq, uint32_t i) {
        return std::byte(uint8_t(seq * 7 + i));
    };

    std::atomic<bool> consumer_failed{ false };

    std::thread producer([&] {
        std::mt19937 rng{ 99 };
        std::uniform_int_distribution<uint32_t> size_dist{ 0, max_payload };

        for (uint64_t seq = 0; seq < n_messages; ++seq)
        {
            const Header header{ seq, size_dist(rng) };
            const size_t total = sizeof(Header) + header.payload_size;

            std::byte* buffer;
            while ((buffer = q.reserve_write(total)) == nullptr)
            {
                if (consumer_failed.load())
                    return;
                std::this_thread::yield();
            }

            std::memcpy(buffer, &header, sizeof(Header));
            for (uint32_t i = 0; i < header.payload_size; ++i)
                buffer[sizeof(Header) + i] = payload_byte(seq, i);
            q.commit_write();
        }
    });

    // Consumer reads header and payload separately like the backend does.
    // Failures are counted instead of using doctest asserts, which aren't
    // meant to be hammered in a hot loop
    uint64_t expected_seq = 0;
    uint64_t bad_seq = 0;
    uint64_t bad_payload = 0;
    uint64_t missing_payload = 0;
    while (expected_seq < n_messages)
    {
        const std::byte* buffer = q.reserve_read(sizeof(Header));
        if (buffer == nullptr)
        {
            std::this_thread::yield();
            continue;
        }

        Header header;
        std::memcpy(&header, buffer, sizeof(Header));
        q.commit_read();

        if (header.seq != expected_seq)
            ++bad_seq;

        if (header.payload_size > 0)
        {
            // Header and payload were committed together, so the payload must
            // be readable right away
            buffer = q.reserve_read(header.payload_size);
            if (buffer == nullptr)
            {
                ++missing_payload;
                consumer_failed.store(true);
                break;
            }
            for (uint32_t i = 0; i < header.payload_size; ++i)
            {
                if (buffer[i] != payload_byte(header.seq, i))
                {
                    ++bad_payload;
                    break;
                }
            }
            q.commit_read();
        }

        ++expected_seq;
    }

    producer.join();

    CHECK(bad_seq == 0);
    CHECK(bad_payload == 0);
    CHECK(missing_payload == 0);
    CHECK(expected_seq == n_messages);
    CHECK(q.reserve_read(1) == nullptr);
}
