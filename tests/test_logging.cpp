// End-to-end tests through the public API: log from one or more threads, let
// the backend write to a file sink and check the file contents.
//
// The backend is asynchronous, so sink/pattern switches and log events are
// only visible once the backend thread got to them. LogCapture deals with this
// by logging unique marker messages and waiting for them to show up in the file.

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

// Included last since it defines macros such as log() and error()
#include "papper/papper.h"

// papper's log() macro clashes with doctest internals (ResultBuilder::log()),
// so it's only enabled where it's used
#pragma push_macro("log")
#undef log

using namespace std::chrono_literals;

namespace
{

constexpr auto timeout = 5s;
constexpr std::string_view marker_prefix = "__papper_marker_";

class LogCapture
{
  public:
    // Redirects logging to a fresh file. Any pattern change must be queued
    // before constructing the capture
    LogCapture()
    {
        static std::atomic<int> counter{ 0 };
        _path = std::filesystem::temp_directory_path()
                / ("papper_test_" + std::to_string(getpid()) + "_"
                   + std::to_string(counter++) + ".log");

        papper::init_thread("main");
        REQUIRE(papper::set_sink(_path.c_str(), true) == 0);

        _start_marker = sync();
    }

    ~LogCapture()
    {
        std::error_code ec;
        std::filesystem::remove(_path, ec);
    }

    // Logs a unique marker from the calling thread and waits until the backend
    // has written it. Since a thread's events are processed in order, all
    // events logged by this thread before the call are then in the file.
    // Returns the marker
    std::string sync()
    {
        static std::atomic<int> counter{ 0 };
        const std::string marker
            = std::string(marker_prefix) + std::to_string(counter++) + "__";

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            // Retried since the marker may be dropped if the queue is full
            error("{}", marker);

            const auto retry_at = std::chrono::steady_clock::now() + 100ms;
            while (std::chrono::steady_clock::now() < retry_at)
            {
                if (read_file().find(marker) != std::string::npos)
                    return marker;
                std::this_thread::sleep_for(1ms);
            }
        }

        FAIL("timed out waiting for marker ", marker);
        return marker;
    }

    // Lines logged after the capture was set up, excluding markers
    std::vector<std::string> lines() const
    {
        std::vector<std::string> result;
        std::istringstream stream{ read_file() };
        bool started = false;
        for (std::string line; std::getline(stream, line);)
        {
            if (line.find(_start_marker) != std::string::npos)
                started = true;
            else if (started && line.find(marker_prefix) == std::string::npos)
                result.push_back(line);
        }
        return result;
    }

    // Waits until at least n lines have been written. Needed when other
    // threads log, since sync() only orders against the calling thread
    std::vector<std::string> wait_for_lines(size_t n) const
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        auto result = lines();
        while (result.size() < n && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(5ms);
            result = lines();
        }
        return result;
    }

  private:
    std::string read_file() const
    {
        std::ifstream file{ _path };
        return { std::istreambuf_iterator<char>(file), {} };
    }

    std::filesystem::path _path;
    std::string _start_marker;
};

// A pattern without timestamps etc. so output can be compared exactly
void set_simple_pattern()
{
    papper_set_pattern("{level}|{thread}|{m}");
}

struct LevelGuard
{
    ~LevelGuard()
    {
        papper::set_level(papper::LogLevel::Trace);
    }
};

} // namespace

TEST_CASE("logging: default pattern has local time, level, location and message")
{
    papper_set_pattern("[{time:HMSf}] ({level}) ({file:nameonly}:{line}) "
                       "{message}");
    LogCapture capture;

    info("hello {}", 42);
    capture.sync();

    const auto lines = capture.lines();
    REQUIRE(lines.size() == 1);

    std::smatch match;
    const std::regex re{ R"(^\[(\d{2}):(\d{2}):(\d{2})\.\d{6}\] \(INFO\) )"
                         R"(\(test_logging\.cpp:\d+\) hello 42$)" };
    REQUIRE_MESSAGE(std::regex_match(lines[0], match, re), lines[0]);

    // The timestamp should be the local wall clock time of the call
    const std::time_t now = std::time(nullptr);
    const std::tm local = *std::localtime(&now);
    const int logged_s = std::stoi(match[1]) * 3600 + std::stoi(match[2]) * 60
                         + std::stoi(match[3]);
    const int now_s = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    int diff = std::abs(now_s - logged_s);
    diff = std::min(diff, 86400 - diff); // Midnight wraparound
    CHECK(diff <= 5);
}

TEST_CASE("logging: every level macro logs with its level")
{
    set_simple_pattern();
    LogCapture capture;

    trace("t");
    debug("d");
    info("i");
    warn("w");
    error("e");
#pragma pop_macro("log")
    log("l"); // Alias for trace
#pragma push_macro("log")
#undef log
    capture.sync();

    CHECK(capture.lines()
          == std::vector<std::string>{ "TRACE|main|t",
                                       "DEBUG|main|d",
                                       "INFO|main|i",
                                       "WARN|main|w",
                                       "ERROR|main|e",
                                       "TRACE|main|l" });
}

TEST_CASE("logging: arguments of different types are formatted")
{
    papper_set_pattern("{m}");
    LogCapture capture;

    info("no args");
    info("{} {} {}", 1, -2, 3'000'000'000LL);
    info("{:.2f} {}", 3.14159, true);
    info("{}{}", 'a', 'b');
    const std::string str = "a string";
    info("[{}] [{}]", str, std::string("temporary"));
    info("{} {} {}", 1, std::string("mixed"), 2.5);
    info("{{literal braces}}");
    info("{1} {0}", "first", "second");
    capture.sync();

    CHECK(capture.lines()
          == std::vector<std::string>{ "no args",
                                       "1 -2 3000000000",
                                       "3.14 true",
                                       "ab",
                                       "[a string] [temporary]",
                                       "1 mixed 2.5",
                                       "{literal braces}",
                                       "second first" });
}

TEST_CASE("logging: string arguments are copied at the call site")
{
    papper_set_pattern("{m}");
    LogCapture capture;

    {
        std::string str = "original";
        info("{}", str);
        str = "modified";
    }
    capture.sync();

    CHECK(capture.lines() == std::vector<std::string>{ "original" });
}

TEST_CASE("logging: events below the minimum level are dropped")
{
    set_simple_pattern();
    LevelGuard guard;
    papper::set_level(papper::LogLevel::Warn);
    LogCapture capture;

    trace("t");
    debug("d");
    info("i");
    warn("w");
    error("e");
    capture.sync();

    CHECK(capture.lines()
          == std::vector<std::string>{ "WARN|main|w", "ERROR|main|e" });

    papper::set_level(papper::LogLevel::Trace);
    trace("back to trace");
    capture.sync();
    CHECK(capture.lines().back() == "TRACE|main|back to trace");
}

TEST_CASE("logging: thread names")
{
    set_simple_pattern();
    LogCapture capture;

    std::thread named([] {
        papper::init_thread("named-worker");
        info("from named");
    });
    named.join();

    std::thread unnamed([] { info("from unnamed"); });
    unnamed.join();

    const auto lines = capture.wait_for_lines(2);
    REQUIRE(lines.size() == 2);

    // The backend may process the two threads in any order
    const bool named_first = lines[0].find("from named") != std::string::npos;
    const std::string& named_line = named_first ? lines[0] : lines[1];
    const std::string& unnamed_line = named_first ? lines[1] : lines[0];

    CHECK(named_line == "INFO|named-worker|from named");
    CHECK(std::regex_match(unnamed_line,
                           std::regex{ R"(INFO\|thread_\d+\|from unnamed)" }));
}

TEST_CASE("logging: many threads, no lost or reordered events")
{
    papper_set_pattern("{thread} {m}");
    LogCapture capture;

    constexpr int n_threads = 4;
    constexpr int n_per_thread = 5000;

    // Threads exit right after logging, so this also checks that events
    // logged just before a thread exits still get written
    std::vector<std::thread> threads;
    for (int t = 0; t < n_threads; ++t)
    {
        threads.emplace_back([t] {
            papper::init_thread("w" + std::to_string(t));
            for (int i = 0; i < n_per_thread; ++i)
                info("{} {}", i, std::string("payload"));
        });
    }
    for (auto& thread : threads)
        thread.join();

    const auto lines = capture.wait_for_lines(n_threads * n_per_thread);
    REQUIRE(lines.size() == n_threads * n_per_thread);

    std::vector<int> next_seq(n_threads, 0);
    int bad_lines = 0;
    const std::regex re{ R"(w(\d+) (\d+) payload)" };
    for (const auto& line : lines)
    {
        std::smatch match;
        if (!std::regex_match(line, match, re))
        {
            ++bad_lines;
            continue;
        }
        const int t = std::stoi(match[1]);
        const int seq = std::stoi(match[2]);
        if (t < 0 || t >= n_threads || seq != next_seq[t])
        {
            ++bad_lines;
            continue;
        }
        ++next_seq[t];
    }

    CHECK(bad_lines == 0);
    for (int t = 0; t < n_threads; ++t)
        CHECK(next_seq[t] == n_per_thread);
}

TEST_CASE("logging: a full queue drops events without corrupting others")
{
    papper_set_pattern("{m}");
    LogCapture capture;

    constexpr int n = 20000;
    std::thread worker([&] {
        papper::init_thread("tiny", 512);
        for (int i = 0; i < n; ++i)
            info("{} {}", i, std::string("some payload to fill the queue"));
        // Later events must still get through once there is room again
        capture.sync();
    });
    worker.join();

    const auto lines = capture.lines();
    REQUIRE(!lines.empty());
    CHECK(lines.size() <= n);
    MESSAGE(lines.size(), " of ", n, " events written");

    int prev = -1;
    int bad_lines = 0;
    const std::regex re{ R"((\d+) some payload to fill the queue)" };
    for (const auto& line : lines)
    {
        std::smatch match;
        if (!std::regex_match(line, match, re) || std::stoi(match[1]) <= prev)
        {
            ++bad_lines;
            continue;
        }
        prev = std::stoi(match[1]);
    }
    CHECK(bad_lines == 0);
}

TEST_CASE("logging: switching sinks redirects output")
{
    papper_set_pattern("{m}");

    LogCapture first;
    info("to first");
    first.sync();

    LogCapture second;
    info("to second");
    second.sync();

    CHECK(first.lines() == std::vector<std::string>{ "to first" });
    CHECK(second.lines() == std::vector<std::string>{ "to second" });
}

TEST_CASE("logging: set_sink returns errno for a file that can't be opened")
{
    CHECK(papper::set_sink("/nonexistent-dir/papper.log") == ENOENT);
}
