#include "bench.h"

#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"

int main(int argc, char** argv)
{
    const bench::Options options = bench::parse_args(argc, argv, "spdlog");

    // One backend thread like the other libraries. The queue is counted in
    // messages, and the frontend blocks when it is full (the default)
    spdlog::init_thread_pool(1 << 17, 1);

    auto logger = spdlog::basic_logger_mt<spdlog::async_factory>(
        "bench", options.log_file, true);

    // Equivalent to papper's default pattern
    logger->set_pattern("[%H:%M:%S.%f] (%l) (%s:%#) %v");

    auto init_thread = [] {};
    auto log = [&logger](int i, double d, const std::string& s) {
        SPDLOG_LOGGER_INFO(
            logger, "Logging int: {}, double: {:.3f}, string: {}", i, d, s);
    };

    bench::Clock::time_point start;
    if (options.mode == bench::Mode::Latency)
        bench::run_latency(options, init_thread, log);
    else
        start = bench::run_throughput(options, init_thread, log);

    // Waits for the backend to write all queued messages and closes the file
    logger.reset();
    spdlog::shutdown();

    if (options.mode == bench::Mode::Throughput)
        bench::report_throughput(options, start);

    bench::check_log_file(options);
}
