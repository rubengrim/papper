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
    const auto res = q.reserve_write(bytes.size());
    if (res.buffer == nullptr)
        return false;
    std::memcpy(res.buffer, bytes.data(), bytes.size());
    q.commit_write(res.new_write_pos);
    return true;
}

// One reserve_read() + commit_read() round of a message of known size
std::optional<std::vector<std::byte>> read_bytes(Queue& q, size_t size)
{
    const auto res = q.reserve_read();
    if (res.buffer == nullptr)
        return std::nullopt;
    std::vector<std::byte> bytes(res.buffer, res.buffer + size);
    q.commit_read(res.current_read_pos + size);
    return bytes;
}

} // namespace

TEST_CASE("queue: empty queue has nothing to read")
{
    Queue q{ 64 };
    CHECK(q.is_empty());
    CHECK(q.reserve_read().buffer == nullptr);
}

TEST_CASE("queue: single write is read back unchanged")
{
    Queue q{ 64 };
    const auto msg = make_bytes(24, 1);

    REQUIRE(write_bytes(q, msg));
    CHECK(!q.is_empty());
    CHECK(read_bytes(q, msg.size()) == msg);
    CHECK(q.is_empty());
    CHECK(q.reserve_read().buffer == nullptr);
}

TEST_CASE("queue: messages are read back one at a time in FIFO order")
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
    CHECK(q.is_empty());
}

TEST_CASE("queue: consecutive writes are laid out contiguously")
{
    Queue q{ 64 };
    const auto r1 = q.reserve_write(10);
    REQUIRE(r1.buffer != nullptr);
    CHECK(r1.new_write_pos == 10);
    q.commit_write(r1.new_write_pos);

    const auto r2 = q.reserve_write(20);
    REQUIRE(r2.buffer != nullptr);
    CHECK(r2.buffer == r1.buffer + 10);
    CHECK(r2.new_write_pos == 30);
}

TEST_CASE("queue: uncommitted write is not visible to reader")
{
    Queue q{ 64 };
    const auto a = make_bytes(8, 1);
    REQUIRE(write_bytes(q, a));

    const auto res = q.reserve_write(8);
    REQUIRE(res.buffer != nullptr);
    CHECK(read_bytes(q, a.size()) == a); // Only the committed write
    CHECK(q.is_empty());

    q.commit_write(res.new_write_pos);
    CHECK(!q.is_empty());
}

TEST_CASE("queue: repeated reservations without commit return the same space")
{
    Queue q{ 64 };
    const auto r1 = q.reserve_write(16);
    const auto r2 = q.reserve_write(16);
    REQUIRE(r1.buffer != nullptr);
    CHECK(r1.buffer == r2.buffer);
    CHECK(r1.new_write_pos == r2.new_write_pos);
    q.commit_write(r2.new_write_pos);

    const auto p1 = q.reserve_read();
    const auto p2 = q.reserve_read();
    CHECK(p1.buffer != nullptr);
    CHECK(p1.buffer == p2.buffer);
    CHECK(p1.buffer == r1.buffer);
    CHECK(p1.current_read_pos == 0);
    CHECK(p2.current_read_pos == 0);
}

TEST_CASE("queue: uncommitted read does not free space")
{
    Queue q{ 64 };
    REQUIRE(write_bytes(q, make_bytes(64, 0)));

    const auto res = q.reserve_read();
    REQUIRE(res.buffer != nullptr);
    CHECK(q.reserve_write(1).buffer == nullptr);

    q.commit_read(res.current_read_pos + 64);
    CHECK(q.reserve_write(1).buffer != nullptr);
}

TEST_CASE("queue: partially committed read frees exactly that much space")
{
    Queue q{ 64 };
    const auto a = make_bytes(24, 1);
    const auto b = make_bytes(40, 2);
    REQUIRE(write_bytes(q, a));
    REQUIRE(write_bytes(q, b));
    CHECK(q.reserve_write(1).buffer == nullptr);

    CHECK(read_bytes(q, a.size()) == a);
    CHECK(q.reserve_write(25).buffer == nullptr);
    CHECK(q.reserve_write(24).buffer != nullptr);

    CHECK(read_bytes(q, b.size()) == b);
    CHECK(q.is_empty());
}

TEST_CASE("queue: capacity is rounded up to a power of two")
{
    Queue q{ 100 }; // -> 128

    const auto msg = make_bytes(128, 7);
    REQUIRE(write_bytes(q, msg));
    CHECK(q.reserve_write(1).buffer == nullptr);
    CHECK(read_bytes(q, msg.size()) == msg);
}

TEST_CASE("queue: reservation larger than capacity fails")
{
    Queue q{ 64 };
    CHECK(q.reserve_write(65).buffer == nullptr);
    CHECK(q.reserve_write(1000).buffer == nullptr);

    // Queue is still usable afterwards
    const auto msg = make_bytes(64, 3);
    REQUIRE(write_bytes(q, msg));
    CHECK(read_bytes(q, msg.size()) == msg);
}

TEST_CASE("queue: full queue rejects writes until reader frees space")
{
    Queue q{ 64 };
    const auto a = make_bytes(32, 1);
    const auto b = make_bytes(32, 2);
    REQUIRE(write_bytes(q, a));
    REQUIRE(write_bytes(q, b));

    CHECK(q.reserve_write(1).buffer == nullptr);

    CHECK(read_bytes(q, a.size()) == a);
    CHECK(read_bytes(q, b.size()) == b);

    // The whole buffer has been freed, so a write wraps around
    const auto c = make_bytes(16, 3);
    REQUIRE(write_bytes(q, c));

    CHECK(read_bytes(q, c.size()) == c);
    CHECK(q.is_empty());
}

TEST_CASE("queue: write crossing the end of the buffer stays contiguous")
{
    Queue q{ 64 };
    const auto first = q.reserve_write(40);
    REQUIRE(first.buffer != nullptr);
    std::byte* const base = first.buffer;
    q.commit_write(first.new_write_pos);
    REQUIRE(read_bytes(q, 40));

    // 40 + 30 > 64, so this spills into the overflow half of the buffer but is
    // still handed out as one contiguous region right after the previous write
    const auto msg = make_bytes(30, 9);
    const auto res = q.reserve_write(msg.size());
    REQUIRE(res.buffer != nullptr);
    CHECK(res.buffer == base + 40);
    CHECK(res.new_write_pos == 70);
    std::memcpy(res.buffer, msg.data(), msg.size());
    q.commit_write(res.new_write_pos);

    const auto read_res = q.reserve_read();
    CHECK(read_res.buffer == base + 40);
    CHECK(read_res.current_read_pos == 40);
    CHECK(read_bytes(q, msg.size()) == msg);
    CHECK(q.is_empty());

    // Positions keep growing, but the next write wraps to the start of the
    // buffer
    const auto next = q.reserve_write(10);
    REQUIRE(next.buffer != nullptr);
    CHECK(next.buffer == base + (70 % 64));
    CHECK(next.new_write_pos == 80);
}

TEST_CASE("queue: wrapping write may use the whole free space")
{
    Queue q{ 64 };
    REQUIRE(write_bytes(q, make_bytes(40, 0)));
    REQUIRE(read_bytes(q, 40));

    // Read and write positions are both 40, so the queue is empty and a write
    // of the full capacity fits even though it crosses the end of the buffer
    CHECK(q.reserve_write(65).buffer == nullptr);
    const auto msg = make_bytes(64, 5);
    REQUIRE(write_bytes(q, msg));
    CHECK(q.reserve_write(1).buffer == nullptr);

    CHECK(read_bytes(q, msg.size()) == msg);
    CHECK(q.is_empty());
}

TEST_CASE("queue: many laps around the buffer")
{
    Queue q{ 64 };

    // 24 doesn't divide 64, so the messages land at every kind of offset,
    // including crossing the end of the buffer
    for (uint8_t i = 0; i < 200; ++i)
    {
        const auto a = make_bytes(24, i);
        const auto b = make_bytes(24, uint8_t(i + 100));
        REQUIRE(write_bytes(q, a));
        REQUIRE(write_bytes(q, b));
        CHECK(read_bytes(q, a.size()) == a);
        CHECK(read_bytes(q, b.size()) == b);
        CHECK(q.is_empty());
    }
}

TEST_CASE("queue: randomized single threaded writes and reads match a model")
{
    Queue q{ 256 };
    std::deque<std::vector<std::byte>> model;
    size_t used = 0;
    std::mt19937 rng{ 1234 };
    std::uniform_int_distribution<size_t> size_dist{ 1, 100 };
    std::bernoulli_distribution do_write{ 0.7 };

    size_t n_written = 0;
    for (int i = 0; i < 100'000; ++i)
    {
        if (do_write(rng))
        {
            const auto msg = make_bytes(size_dist(rng), uint8_t(i));
            const bool ok = write_bytes(q, msg);
            // A write must succeed exactly when there is room for it
            REQUIRE(ok == (used + msg.size() <= 256));
            if (ok)
            {
                used += msg.size();
                model.push_back(msg);
                ++n_written;
            }
        }
        else if (model.empty())
        {
            REQUIRE(q.is_empty());
            REQUIRE(q.reserve_read().buffer == nullptr);
        }
        else
        {
            // Everything committed must be readable
            const auto& expected = model.front();
            const auto msg = read_bytes(q, expected.size());
            REQUIRE(msg);
            REQUIRE(*msg == expected);
            used -= expected.size();
            model.pop_front();
        }
    }

    while (!model.empty())
    {
        REQUIRE(read_bytes(q, model.front().size()) == model.front());
        model.pop_front();
    }
    CHECK(q.is_empty());
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

    auto payload_byte = [](uint64_t seq, uint32_t i) {
        return std::byte(uint8_t(seq * 7 + i));
    };

    // Small capacities force lots of wrapping and full-queue cases
    for (const size_t capacity : { 64, 256, 4096 })
    {
        CAPTURE(capacity);

        Queue q{ capacity };
        // Messages may be as large as the whole queue
        const uint32_t max_payload
            = uint32_t(std::min<size_t>(200, capacity - sizeof(Header)));
        std::atomic<bool> consumer_failed{ false };

        std::thread producer([&] {
            std::mt19937 rng{ 99 };
            std::uniform_int_distribution<uint32_t> size_dist{ 0,
                                                               max_payload };

            for (uint64_t seq = 0; seq < n_messages; ++seq)
            {
                const Header header{ seq, size_dist(rng) };
                const size_t total = sizeof(Header) + header.payload_size;

                while (true)
                {
                    const auto res = q.reserve_write(total);
                    if (res.buffer != nullptr)
                    {
                        std::memcpy(res.buffer, &header, sizeof(Header));
                        for (uint32_t i = 0; i < header.payload_size; ++i)
                            res.buffer[sizeof(Header) + i]
                                = payload_byte(seq, i);
                        q.commit_write(res.new_write_pos);
                        break;
                    }
                    if (consumer_failed.load())
                        return;
                    std::this_thread::yield();
                }
            }
        });

        // Consumer reads one message at a time, using the header to know how
        // much to commit, like the backend does. Failures are counted instead
        // of using doctest asserts, which aren't meant to be hammered in a hot
        // loop
        uint64_t expected_seq = 0;
        uint64_t bad_seq = 0;
        uint64_t bad_size = 0;
        uint64_t bad_payload = 0;
        while (expected_seq < n_messages && !consumer_failed.load())
        {
            const auto res = q.reserve_read();
            if (res.buffer == nullptr)
            {
                std::this_thread::yield();
                continue;
            }

            Header header;
            std::memcpy(&header, res.buffer, sizeof(Header));
            if (header.payload_size > max_payload)
            {
                // Garbage header, can't know where the next message starts
                ++bad_size;
                consumer_failed.store(true);
                break;
            }

            if (header.seq != expected_seq)
                ++bad_seq;

            const std::byte* payload = res.buffer + sizeof(Header);
            for (uint32_t i = 0; i < header.payload_size; ++i)
            {
                if (payload[i] != payload_byte(header.seq, i))
                {
                    ++bad_payload;
                    break;
                }
            }

            q.commit_read(res.current_read_pos + sizeof(Header)
                          + header.payload_size);
            ++expected_seq;
        }

        producer.join();

        CHECK(bad_seq == 0);
        CHECK(bad_size == 0);
        CHECK(bad_payload == 0);
        CHECK(expected_seq == n_messages);
        CHECK(q.is_empty());
    }
}
