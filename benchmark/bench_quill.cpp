#include "bench.h"

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/FileSink.h"

int main(int argc, char** argv)
{
    const bench::Options options = bench::parse_args(argc, argv, "quill");

    // Don't print a notice each time a queue grows, but keep real errors
    quill::BackendOptions backend_options;
    backend_options.error_notifier = [](const std::string& message) {
        if (message.find("Allocated a new SPSC queue") == std::string::npos)
            std::fprintf(stderr, "%s\n", message.c_str());
    };
    quill::Backend::start(backend_options);

    auto sink = quill::Frontend::create_or_get_sink<quill::FileSink>(
        options.log_file,
        [] {
            quill::FileSinkConfig config;
            config.set_open_mode('w');
            config.set_write_buffer_size(64 * 1024);
            return config;
        }(),
        quill::FileEventNotifier{});

    // Equivalent to papper's default pattern
    quill::Logger* logger = quill::Frontend::create_or_get_logger(
        "bench",
        std::move(sink),
        quill::PatternFormatterOptions{
            "[%(time)] (%(log_level)) (%(file_name):%(line_number)) "
            "%(message)",
            "%H:%M:%S.%Qus" });

    // Allocates the thread's queue up front
    auto init_thread = [] { quill::Frontend::preallocate(); };
    auto log = [logger](int i, double d, const std::string& s) {
        LOG_INFO(logger, "Logging int: {}, double: {:.3f}, string: {}", i, d, s);
    };

    if (options.mode == bench::Mode::Latency)
    {
        bench::run_latency(options, init_thread, log);
        logger->flush_log();
    }
    else
    {
        const auto start = bench::run_throughput(options, init_thread, log);
        logger->flush_log();
        bench::report_throughput(options, start);
    }

    bench::check_log_file(options);
}
