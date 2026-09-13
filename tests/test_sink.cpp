#include <catch2/catch_test_macros.hpp>
#include <papper/detail/sink.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("SinkHandler writes to file sink", "[sink]")
{
    fs::path temp_file = fs::temp_directory_path() / "papper_sink_test1.txt";

    // Scope the SinkHandler so its destructor runs and closes any open files
    {
        papper::detail::SinkHandler sink_handler;

        FILE* f = std::fopen(temp_file.string().c_str(), "w+");
        REQUIRE(f != nullptr);

        sink_handler.queue_new_sink(f, _IONBF, 0);
        sink_handler.poll_for_pending_sink_switch();

        sink_handler.write("Hello ");
        sink_handler.write_str_and_endl("World!");
    }

    std::ifstream in(temp_file);
    REQUIRE(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    in.close();
    fs::remove(temp_file);

    REQUIRE(content == "Hello World!\n");
}

TEST_CASE("SinkHandler sink switching", "[sink]")
{
    fs::path temp1 = fs::temp_directory_path() / "papper_sink_switch1.txt";
    fs::path temp2 = fs::temp_directory_path() / "papper_sink_switch2.txt";

    {
        papper::detail::SinkHandler sink_handler;

        FILE* f1 = std::fopen(temp1.string().c_str(), "w+");
        REQUIRE(f1 != nullptr);
        sink_handler.queue_new_sink(f1, _IONBF, 0);
        sink_handler.poll_for_pending_sink_switch();

        sink_handler.write_str_and_endl("Message in first sink");

        // Switch to second sink
        FILE* f2 = std::fopen(temp2.string().c_str(), "w+");
        REQUIRE(f2 != nullptr);
        sink_handler.queue_new_sink(f2, _IONBF, 0);
        sink_handler.poll_for_pending_sink_switch();

        sink_handler.write_str_and_endl("Message in second sink");
    }

    // Verify temp1
    {
        std::ifstream in1(temp1);
        std::string content1((std::istreambuf_iterator<char>(in1)),
                             std::istreambuf_iterator<char>());
        REQUIRE(content1 == "Message in first sink\n");
    }

    // Verify temp2
    {
        std::ifstream in2(temp2);
        std::string content2((std::istreambuf_iterator<char>(in2)),
                             std::istreambuf_iterator<char>());
        REQUIRE(content2 == "Message in second sink\n");
    }

    fs::remove(temp1);
    fs::remove(temp2);
}
