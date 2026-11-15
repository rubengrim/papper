#include "bench.h"

#include "fmtlog.h"

int main(int argc, char** argv)
{
    const bench::Options options = bench::parse_args(argc, argv, "fmtlog");

    // Queue size and blocking when full are set at compile time, see
    // CMakeLists.txt
    fmtlog::setLogFile(options.log_file.c_str(), true);
    fmtlog::setFlushBufSize(64 * 1024);

    // Equivalent to papper's default pattern. {s} is the file name and line
    fmtlog::setHeaderPattern("[{HMSf}] ({l}) ({s}) ");

    // The polling thread is fmtlog's backend. It sleeps for whatever is left
    // of the interval after each poll (default 1s), so use papper's maximum
    // backoff
    fmtlog::startPollingThread(200'000);

    // Allocates the thread's queue up front
    auto init_thread = [] {
        fmtlog::preallocate();
        fmtlog::setThreadName("bench");
    };
    auto log = [](int i, double d, const std::string& s) {
        logi("Logging int: {}, double: {:.3f}, string: {}", i, d, s);
    };

    bench::Clock::time_point start;
    if (options.mode == bench::Mode::Latency)
        bench::run_latency(options, init_thread, log);
    else
        start = bench::run_throughput(options, init_thread, log);

    // Joins the polling thread, which writes and flushes all queued messages
    // before exiting
    fmtlog::stopPollingThread();

    if (options.mode == bench::Mode::Throughput)
        bench::report_throughput(options, start);

    fmtlog::closeLogFile();
    bench::check_log_file(options);
}
