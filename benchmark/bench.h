#pragma once

// Shared harness for the logger benchmarks. Each bench_<library>.cpp sets up
// its logger, and hands the harness two callables:
//   init_thread()                          called once on each benchmark
//   thread
//                                          before it starts logging
//   log(int, double, const std::string&)   logs one message
//
// Two things are measured:
//   latency     How long a log call blocks the calling thread. Messages are
//               logged in short bursts with pauses in between so the backend
//               can keep up, i.e. queues never fill up.
//   throughput  How many messages per second end up in the log file. Messages
//               are logged as fast as possible and the clock is stopped once
//               the backend has written all of them.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <latch>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace bench
{

using Clock = std::chrono::steady_clock;

enum class Mode
{
    Latency,
    Throughput,
};

struct Options
{
    const char* library = "";
    Mode mode = Mode::Latency;
    int threads = 1;
    std::string log_file;

    // Latency: each thread logs `bursts` bursts of `burst_size` messages with
    // a `pause_us` pause after each burst. A whole burst is timed at once
    // since clocks are too coarse to time a single call on some platforms
    // (e.g. 41.7ns on Apple silicon). The result is the average cost per call
    // within a burst: the CPU overlaps consecutive calls, so it is lower than
    // the latency of a single isolated call. Use --burst-size 1 where the
    // clock is precise enough to time single calls.
    int bursts = 20000;
    int burst_size = 100;
    int pause_us = 1000;

    // Throughput: total number of messages over all threads
    int messages = 1'000'000;
};

// The argument logged with each message, to include a string copy in the cost
inline const std::string string_arg = "a string argument";

// Number of messages the benchmark logged, to check the log file against
inline long expected_lines = 0;

[[noreturn]] inline void usage(const char* program)
{
    std::fprintf(stderr,
                 "Usage: %s latency|throughput [--threads N] "
                 "[--log-file PATH]\n"
                 "  latency:    [--bursts N] [--burst-size N] [--pause-us N]\n"
                 "  throughput: [--messages N]\n",
                 program);
    std::exit(1);
}

inline Options parse_args(int argc, char** argv, const char* library)
{
    Options opt;
    opt.library = library;
    opt.log_file = std::string(library) + ".log";

    if (argc < 2)
        usage(argv[0]);
    const std::string_view mode = argv[1];
    if (mode == "latency")
        opt.mode = Mode::Latency;
    else if (mode == "throughput")
        opt.mode = Mode::Throughput;
    else
        usage(argv[0]);

    for (int i = 2; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (i + 1 >= argc)
            usage(argv[0]);
        const char* value = argv[++i];

        if (arg == "--threads")
            opt.threads = std::atoi(value);
        else if (arg == "--log-file")
            opt.log_file = value;
        else if (arg == "--bursts")
            opt.bursts = std::atoi(value);
        else if (arg == "--burst-size")
            opt.burst_size = std::atoi(value);
        else if (arg == "--pause-us")
            opt.pause_us = std::atoi(value);
        else if (arg == "--messages")
            opt.messages = std::atoi(value);
        else
            usage(argv[0]);
    }

    if (opt.threads < 1 || opt.bursts < 1 || opt.burst_size < 1
        || opt.messages < opt.threads)
        usage(argv[0]);

    return opt;
}

// Runs body(thread_index) on n threads. All threads call init_thread() first
// and then start the body at the same time.
// Returns the time the bodies were started
template <typename InitThread, typename Body>
Clock::time_point run_threads(int n, InitThread& init_thread, Body body)
{
    std::latch ready{ n + 1 };
    std::vector<std::thread> threads;
    for (int t = 0; t < n; ++t)
    {
        threads.emplace_back([&, t] {
            init_thread();
            ready.arrive_and_wait();
            body(t);
        });
    }

    ready.arrive_and_wait();
    const Clock::time_point start = Clock::now();

    for (auto& thread : threads)
        thread.join();

    return start;
}

template <typename InitThread, typename Log>
void run_latency(const Options& opt, InitThread init_thread, Log log)
{
    const auto pause = std::chrono::microseconds(opt.pause_us);
    const int warmup_bursts = std::max(opt.bursts / 4, 1);

    // Average ns per call of each burst, per thread
    std::vector<std::vector<double>> samples(opt.threads);

    run_threads(opt.threads, init_thread, [&](int t) {
        samples[t].reserve(opt.bursts);
        for (int b = 0; b < warmup_bursts + opt.bursts; ++b)
        {
            const Clock::time_point burst_start = Clock::now();
            for (int i = 0; i < opt.burst_size; ++i)
                log(i, i * 0.5, string_arg);
            const Clock::time_point burst_end = Clock::now();

            if (b >= warmup_bursts)
            {
                const std::chrono::duration<double, std::nano> elapsed
                    = burst_end - burst_start;
                samples[t].push_back(elapsed.count() / opt.burst_size);
            }

            // Busy-wait instead of sleeping, since after a sleep the core may
            // have clocked down or gone idle. That would be measured too and
            // depend on how often each library's backend thread wakes up
            const Clock::time_point pause_end = Clock::now() + pause;
            while (Clock::now() < pause_end)
            {
            }
        }
    });

    expected_lines
        = (long)opt.threads * (warmup_bursts + opt.bursts) * opt.burst_size;

    std::vector<double> all;
    for (const auto& thread_samples : samples)
        all.insert(all.end(), thread_samples.begin(), thread_samples.end());
    std::sort(all.begin(), all.end());
    double mean = 0;
    for (const double sample : all)
        mean += sample / all.size();

    auto percentile = [&](double p) {
        return all[std::min((size_t)(p / 100.0 * all.size()), all.size() - 1)];
    };

    std::printf("%-7s latency     threads %-2d  ns/call  mean %7.1f  p50 "
                "%7.1f  p90 %7.1f  "
                "p99 %7.1f  p99.9 %7.1f  max %8.1f\n",
                opt.library,
                opt.threads,
                mean,
                percentile(50),
                percentile(90),
                percentile(99),
                percentile(99.9),
                all.back());
}

// Logs opt.messages messages as fast as possible.
// Returns the start time, to pass to report_throughput() once everything has
// been written
template <typename InitThread, typename Log>
Clock::time_point run_throughput(const Options& opt, InitThread init_thread,
                                 Log log)
{
    const int per_thread = opt.messages / opt.threads;
    expected_lines = (long)per_thread * opt.threads;

    return run_threads(opt.threads, init_thread, [&](int) {
        for (int i = 0; i < per_thread; ++i)
            log(i, i * 0.5, string_arg);
    });
}

// Call once the backend has written all messages to the log file
inline void report_throughput(const Options& opt, Clock::time_point start)
{
    const std::chrono::duration<double> elapsed = Clock::now() - start;

    std::printf("%-7s throughput  threads %-2d  %ld msgs in %.3f s  "
                "=  %.2f M msgs/s\n",
                opt.library,
                opt.threads,
                expected_lines,
                elapsed.count(),
                expected_lines / elapsed.count() / 1e6);
}

// Checks that no messages were dropped. Call once the log file is complete
inline void check_log_file(const Options& opt)
{
    std::ifstream file{ opt.log_file, std::ios::binary };
    const long lines = std::count(std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>(),
                                  '\n');
    if (lines != expected_lines)
    {
        std::printf("%-7s WARNING: %ld of %ld messages were written to %s\n",
                    opt.library,
                    lines,
                    expected_lines,
                    opt.log_file.c_str());
    }
}

} // namespace bench
