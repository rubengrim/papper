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

std::vector<std::byte> concat(const std::vector<std::vector<std::byte>>& parts)
{
    std::vector<std::byte> bytes;
    for (const auto& part : parts)
        bytes.insert(bytes.end(), part.begin(), part.end());
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

// One read_all_available() + commit_read() round
std::optional<std::vector<std::byte>> read_available(Queue& q)
{
    size_t n_read = 123; // Must be overwritten
    const std::byte* buffer = q.read_all_available(n_read);
    if (buffer == nullptr)
    {
        CHECK(n_read == 0);
        return std::nullopt;
    }
    CHECK(n_read > 0);
    std::vector<std::byte> bytes(buffer, buffer + n_read);
    q.commit_read();
    return bytes;
}

// Reads until the queue is empty. Needed since a wrapped queue is read in two
// rounds (tail up to the end marker, then the front)
std::vector<std::byte> drain(Queue& q)
{
    std::vector<std::byte> bytes;
    while (const auto chunk = read_available(q))
        bytes.insert(bytes.end(), chunk->begin(), chunk->end());
    return bytes;
}

bool is_empty(Queue& q)
{
    size_t n_read = 123;
    const bool empty = q.read_all_available(n_read) == nullptr;
    return empty && n_read == 0;
}

} // namespace

TEST_CASE("queue: empty queue has nothing to read")
{
    Queue q{ 64 };
    CHECK(is_empty(q));
}

TEST_CASE("queue: single write is read back unchanged")
{
    Queue q{ 64 };
    const auto msg = make_bytes(24, 1);

    REQUIRE(write_bytes(q, msg));
    CHECK(read_available(q) == msg);
    CHECK(is_empty(q));
}

TEST_CASE("queue: all available messages are read at once in FIFO order")
{
    Queue q{ 256 };
    std::vector<std::vector<std::byte>> msgs;
    for (uint8_t i = 0; i < 10; ++i)
    {
        msgs.push_back(make_bytes(5 + i, i));
        REQUIRE(write_bytes(q, msgs.back()));
    }

    CHECK(read_available(q) == concat(msgs));
    CHECK(is_empty(q));
}

TEST_CASE("queue: uncommitted write is not visible to reader")
{
    Queue q{ 64 };
    const auto a = make_bytes(8, 1);
    REQUIRE(write_bytes(q, a));

    std::byte* buffer = q.reserve_write(8);
    REQUIRE(buffer != nullptr);
    CHECK(read_available(q) == a); // Only the committed write
    CHECK(is_empty(q));

    q.commit_write();
    CHECK(!is_empty(q));
}

TEST_CASE("queue: uncommitted read does not free space")
{
    Queue q{ 64 };
    REQUIRE(write_bytes(q, make_bytes(64, 0)));

    size_t n_read = 0;
    REQUIRE(q.read_all_available(n_read) != nullptr);
    CHECK(n_read == 64);
    CHECK(q.reserve_write(1) == nullptr);

    q.commit_read();
    CHECK(q.reserve_write(1) != nullptr);
}

TEST_CASE("queue: repeated reads without commit return the same region")
{
    Queue q{ 64 };
    const auto a = make_bytes(20, 1);
    REQUIRE(write_bytes(q, a));

    size_t n1 = 0;
    size_t n2 = 0;
    const std::byte* p1 = q.read_all_available(n1);
    const std::byte* p2 = q.read_all_available(n2);
    CHECK(p1 != nullptr);
    CHECK(p1 == p2);
    CHECK(n1 == n2);
    q.commit_read();
    CHECK(is_empty(q));
}

TEST_CASE("queue: capacity is rounded up to a power of two")
{
    Queue q{ 100 }; // -> 128

    const auto msg = make_bytes(128, 7);
    REQUIRE(write_bytes(q, msg));
    CHECK(q.reserve_write(1) == nullptr);
    CHECK(read_available(q) == msg);
}

TEST_CASE("queue: reservation larger than capacity fails")
{
    Queue q{ 64 };
    CHECK(q.reserve_write(65) == nullptr);
    CHECK(q.reserve_write(1000) == nullptr);

    // Queue is still usable afterwards
    const auto msg = make_bytes(64, 3);
    REQUIRE(write_bytes(q, msg));
    CHECK(read_available(q) == msg);
}

TEST_CASE("queue: full queue rejects writes until reader frees space")
{
    Queue q{ 64 };
    const auto a = make_bytes(32, 1);
    const auto b = make_bytes(32, 2);
    REQUIRE(write_bytes(q, a));
    REQUIRE(write_bytes(q, b));

    CHECK(q.reserve_write(1) == nullptr);

    CHECK(read_available(q) == concat({ a, b }));

    // The whole buffer has been freed, so a write wraps around
    const auto c = make_bytes(16, 3);
    REQUIRE(write_bytes(q, c));

    CHECK(read_available(q) == c);
    CHECK(is_empty(q));
}

TEST_CASE("queue: write that doesn't fit at the tail wraps to the front")
{
    Queue q{ 64 };
    REQUIRE(write_bytes(q, make_bytes(40, 0)));
    REQUIRE(read_available(q));

    // 40 + 30 > 64, so this has to be placed at the start of the buffer. The
    // reader is exactly at the end marker, so it must not return the unused
    // tail [40, 64) but go straight to the front
    const auto msg = make_bytes(30, 9);
    REQUIRE(write_bytes(q, msg));
    CHECK(read_available(q) == msg);
    CHECK(is_empty(q));
}

TEST_CASE("queue: wrapping write needs strictly more room than its size")
{
    Queue q{ 64 };
    REQUIRE(write_bytes(q, make_bytes(40, 0)));
    REQUIRE(read_available(q));

    // Read index is at 40. Wrapping a 40 byte write would make the write index
    // catch up with the read index, which would look like an empty queue
    CHECK(q.reserve_write(40) == nullptr);
    CHECK(q.reserve_write(39) != nullptr);
}

TEST_CASE("queue: wrapped queue is read as tail up to end marker, then front")
{
    Queue q{ 64 };
    REQUIRE(write_bytes(q, make_bytes(40, 0)));
    REQUIRE(read_available(q)); // read index = 40

    const auto a = make_bytes(20, 1);
    const auto b = make_bytes(30, 2);
    REQUIRE(write_bytes(q, a)); // write index = 60
    REQUIRE(write_bytes(q, b)); // 60 + 30 > 64, wraps with end marker at 60

    // Reading the tail twice without committing must not move past the end
    // marker or into the unused bytes [60, 64)
    size_t n1 = 0;
    size_t n2 = 0;
    const std::byte* p1 = q.read_all_available(n1);
    const std::byte* p2 = q.read_all_available(n2);
    REQUIRE(p1 != nullptr);
    CHECK(p1 == p2);
    CHECK(n1 == a.size());
    CHECK(n2 == a.size());
    CHECK(std::vector<std::byte>(p1, p1 + n1) == a);
    q.commit_read();

    CHECK(read_available(q) == b);
    CHECK(is_empty(q));

    // The end marker must have been reset, so the queue keeps working
    // normally across another lap that wraps again
    const auto c = make_bytes(30, 3);
    const auto d = make_bytes(20, 4);
    REQUIRE(write_bytes(q, c)); // write index = 60
    REQUIRE(write_bytes(q, d)); // wraps with end marker at 60
    CHECK(drain(q) == concat({ c, d }));
    CHECK(is_empty(q));
}

TEST_CASE("queue: writer behind a wrapped reader leaves a one byte gap")
{
    Queue q{ 64 };
    const auto a = make_bytes(20, 1);
    const auto b = make_bytes(20, 2);
    const auto c = make_bytes(24, 3);
    const auto d = make_bytes(10, 4);
    const auto e = make_bytes(9, 5);

    REQUIRE(write_bytes(q, a));
    CHECK(read_available(q) == a); // read index = 20

    REQUIRE(write_bytes(q, b)); // write index = 40
    REQUIRE(write_bytes(q, c)); // fills tail exactly, write index = 64
    REQUIRE(write_bytes(q, d)); // wraps, write index = 10
    REQUIRE(write_bytes(q, e)); // write index = 19, one byte before read
    CHECK(q.reserve_write(1) == nullptr);

    CHECK(read_available(q) == concat({ b, c }));
    CHECK(read_available(q) == concat({ d, e }));
    CHECK(is_empty(q));
}

TEST_CASE("queue: randomized single threaded writes and reads match a model")
{
    Queue q{ 256 };
    std::deque<std::vector<std::byte>> model;
    std::mt19937 rng{ 1234 };
    std::uniform_int_distribution<size_t> size_dist{ 1, 100 };
    std::bernoulli_distribution do_write{ 0.7 };

    // Pops whole messages off the model matching a chunk read from the queue.
    // A chunk must always end on a message boundary
    auto consume = [&](const std::vector<std::byte>& chunk) {
        size_t offset = 0;
        while (offset < chunk.size())
        {
            REQUIRE(!model.empty());
            const auto& msg = model.front();
            REQUIRE(offset + msg.size() <= chunk.size());
            REQUIRE(std::equal(msg.begin(), msg.end(), chunk.begin() + offset));
            offset += msg.size();
            model.pop_front();
        }
    };

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
            REQUIRE(is_empty(q));
        }
        else
        {
            // Everything committed must be readable
            const auto chunk = read_available(q);
            REQUIRE(chunk);
            consume(*chunk);
        }
    }

    consume(drain(q));
    CHECK(model.empty());
    CHECK(is_empty(q));
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
        const uint32_t max_payload
            = uint32_t(std::min<size_t>(200, capacity / 2 - sizeof(Header)));
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

        // Consumer copies everything available into a local buffer, commits
        // and then parses messages from the copy, like the backend does.
        // Failures are counted instead of using doctest asserts, which aren't
        // meant to be hammered in a hot loop
        std::vector<std::byte> local(capacity);
        uint64_t expected_seq = 0;
        uint64_t bad_seq = 0;
        uint64_t bad_payload = 0;
        uint64_t split_message = 0;
        while (expected_seq < n_messages && !consumer_failed.load())
        {
            size_t n_read = 0;
            const std::byte* buffer = q.read_all_available(n_read);
            if (buffer == nullptr)
            {
                std::this_thread::yield();
                continue;
            }

            std::memcpy(local.data(), buffer, n_read);
            q.commit_read();

            size_t offset = 0;
            while (offset < n_read)
            {
                // A message must never be split between two reads
                Header header;
                if (n_read - offset < sizeof(Header))
                {
                    ++split_message;
                    consumer_failed.store(true);
                    break;
                }
                std::memcpy(&header, &local[offset], sizeof(Header));
                if (n_read - offset < sizeof(Header) + header.payload_size)
                {
                    ++split_message;
                    consumer_failed.store(true);
                    break;
                }

                if (header.seq != expected_seq)
                    ++bad_seq;

                const std::byte* payload = &local[offset + sizeof(Header)];
                for (uint32_t i = 0; i < header.payload_size; ++i)
                {
                    if (payload[i] != payload_byte(header.seq, i))
                    {
                        ++bad_payload;
                        break;
                    }
                }

                offset += sizeof(Header) + header.payload_size;
                ++expected_seq;
            }
        }

        producer.join();

        CHECK(bad_seq == 0);
        CHECK(bad_payload == 0);
        CHECK(split_message == 0);
        CHECK(expected_seq == n_messages);
        CHECK(is_empty(q));
    }
}
