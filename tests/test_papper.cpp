#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <papper/papper.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;

namespace
{
std::string wait_and_read_file(const fs::path& file_path,
                               std::string_view expected_marker,
                               int max_wait_ms = 1000)
{
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(max_wait_ms);
    std::string content;

    while (std::chrono::steady_clock::now() < deadline)
    {
        if (fs::exists(file_path))
        {
            std::ifstream in(file_path);
            if (in.is_open())
            {
                content.assign((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
                if (content.find(expected_marker) != std::string::npos)
                {
                    return content;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return content;
}
} // namespace

TEST_CASE("Papper end-to-end logging and pattern", "[papper]")
{
    fs::path log_path = fs::temp_directory_path() / "papper_e2e_test.log";

    int sink_res = papper::set_sink(log_path.string().c_str(), true);
    REQUIRE(sink_res == 0);

    papper_set_pattern("[{level}] {message}");
    papper::set_level(papper::LogLevel::Trace);

    // Give the backend thread a moment to switch sink and pattern
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    SECTION("Logs different levels with formatting")
    {
        info("Server listening on port {}", 8080);
        warn("Resource usage high: {}%", 88);

        std::string content
            = wait_and_read_file(log_path, "Resource usage high: 88%");
        REQUIRE_THAT(
            content,
            ContainsSubstring("[INFO] Server listening on port 8080"));
        REQUIRE_THAT(content,
                     ContainsSubstring("[WARN] Resource usage high: 88%"));
    }

    SECTION("Log level filtering drops events below threshold")
    {
        // Truncate file by setting sink again
        REQUIRE(papper::set_sink(log_path.string().c_str(), true) == 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        papper::set_level(papper::LogLevel::Warn);

        trace("Filtered trace event");
        debug("Filtered debug event");
        info("Filtered info event");
        warn("Visible warn event");
        error("Visible error event: code {}", 500);

        std::string content
            = wait_and_read_file(log_path, "Visible error event: code 500");

        REQUIRE_THAT(content, !ContainsSubstring("Filtered trace event"));
        REQUIRE_THAT(content, !ContainsSubstring("Filtered debug event"));
        REQUIRE_THAT(content, !ContainsSubstring("Filtered info event"));
        REQUIRE_THAT(content, ContainsSubstring("[WARN] Visible warn event"));
        REQUIRE_THAT(
            content,
            ContainsSubstring("[ERROR] Visible error event: code 500"));
    }

    SECTION("Thread naming in output")
    {
        REQUIRE(papper::set_sink(log_path.string().c_str(), true) == 0);
        papper_set_pattern("[{thread}] {message}");
        papper::set_level(papper::LogLevel::Info);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::thread worker([]() {
            papper::init_thread("WorkerAlpha");
            info("Message from thread {}", 1);
        });
        worker.join();

        std::string content = wait_and_read_file(log_path, "WorkerAlpha");
        REQUIRE_THAT(content,
                     ContainsSubstring("[WorkerAlpha] Message from thread 1"));
    }

    fs::remove(log_path);
}
