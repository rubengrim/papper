#include <cstdio>
#include <cstdlib>

#include "bench.h"
#include "papper/papper.h"

namespace
{

bench::Options options;
bench::Clock::time_point throughput_start;

// papper has no flush call, its backend only finishes writing all queued
// messages and closes the sink when it is destroyed at exit. This is
// registered with atexit before the backend is created, so it runs after the
// backend's destructor, i.e. once everything has been written.
void finish()
{
    if (options.mode == bench::Mode::Throughput)
        bench::report_throughput(options, throughput_start);
    bench::check_log_file(options);
}

} // namespace

int main(int argc, char** argv)
{
    options = bench::parse_args(argc, argv, "papper");

    // Must happen before anything creates the backend, see finish()
    std::atexit(finish);

    // papper drops messages when a queue is full, so for throughput make the
    // queues big enough to hold every message (~70 bytes each)
    if (options.mode == bench::Mode::Throughput)
        papper::set_default_queue_size((size_t)options.messages
                                       / options.threads * 128);

    // The default pattern is "[{time:HMSf}] ({level}) ({file:nameonly}:{line})
    // {message}", the other benchmarks use an equivalent one
    if (papper::set_sink(options.log_file.c_str(),
                         true,
                         papper::FlushOn::BufferFull,
                         64 * 1024)
        != 0)
    {
        std::perror("set_sink");
        return 1;
    }

    auto init_thread = [] { papper::init_thread("bench"); };
    auto log = [](int i, double d, const std::string& s) {
        PAPPER_INFO("Logging int: {}, double: {:.3f}, string: {}", i, d, s);
    };

    if (options.mode == bench::Mode::Latency)
        bench::run_latency(options, init_thread, log);
    else
        throughput_start = bench::run_throughput(options, init_thread, log);

    // Results are reported by finish() once the backend has shut down
}
