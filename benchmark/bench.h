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
//
// With --affinity same|split the benchmark threads and the backend thread are
// pinned to CPUs, either all in the same CPU cluster (CPUs sharing an L3
// cache) or with the backend in one cluster and the benchmark threads in
// another. Each thread gets a core of its own while there are enough, see
// place_threads(). Pinning the backend is up to each bench_<library>.cpp, via
// Options::backend_cpu.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <latch>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace bench
{

using Clock = std::chrono::steady_clock;

enum class Mode
{
    Latency,
    Throughput,
};

enum class Affinity
{
    None,  // Don't pin any threads
    Same,  // Backend and benchmark threads in the same CPU cluster
    Split, // Backend in one CPU cluster, benchmark threads in another
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
    int bursts = 3000;
    int burst_size = 100;
    int pause_us = 1000;

    // Throughput: total number of messages over all threads
    int messages = 1'000'000;

    Affinity affinity = Affinity::None;
    // Set by parse_args() from `affinity`. -1 / empty when not pinning
    int backend_cpu = -1;
    std::vector<int> frontend_cpus; // Benchmark thread t uses [t % size]
};

// The argument logged with each message, to include a string copy in the cost
inline const std::string string_arg = "a string argument";

// Number of messages the benchmark logged, to check the log file against
inline long expected_lines = 0;

[[noreturn]] inline void usage(const char* program)
{
    std::fprintf(stderr,
                 "Usage: %s latency|throughput [--threads N] "
                 "[--log-file PATH] [--affinity none|same|split]\n"
                 "  latency:    [--bursts N] [--burst-size N] [--pause-us N]\n"
                 "  throughput: [--messages N]\n",
                 program);
    std::exit(1);
}

// Parses a sysfs CPU list such as "0-2,6-8". Empty if the file is missing
inline std::vector<int> read_cpu_list(const std::string& path)
{
    std::vector<int> cpus;
    std::ifstream file{ path };
    std::string range;
    while (std::getline(file, range, ','))
    {
        int first = 0;
        int last = 0;
        const int n = std::sscanf(range.c_str(), "%d-%d", &first, &last);
        if (n < 1)
            continue;
        if (n == 1)
            last = first;
        for (int cpu = first; cpu <= last; ++cpu)
            cpus.push_back(cpu);
    }
    return cpus;
}

// Picks the CPUs for the backend and the benchmark threads, see Affinity.
// Clusters are the groups of CPUs sharing an L3 cache. Within a cluster the
// first hardware thread of each core is used before any SMT siblings, and the
// backend's core is never shared with a benchmark thread.
inline void place_threads(Options& opt)
{
    if (opt.affinity == Affinity::None)
        return;

    auto fail = [](const char* message) {
        std::fprintf(stderr, "--affinity: %s\n", message);
        std::exit(1);
    };

    const std::string sys = "/sys/devices/system/cpu/";
    const std::vector<int> online = read_cpu_list(sys + "online");
    if (online.empty())
        fail("can't read the CPU topology (Linux only)");

    // Key: first CPU of the L3 cache's CPU list. Value: (SMT sibling index,
    // CPU), so sorting puts one hardware thread per core first
    std::map<int, std::vector<std::pair<int, int>>> clusters;
    std::map<int, std::vector<int>> siblings_of;
    for (const int cpu : online)
    {
        const std::string dir = sys + "cpu" + std::to_string(cpu) + "/";
        const std::vector<int> l3
            = read_cpu_list(dir + "cache/index3/shared_cpu_list");
        std::vector<int> siblings
            = read_cpu_list(dir + "topology/thread_siblings_list");
        if (siblings.empty())
            siblings = { cpu };

        const int cluster = l3.empty() ? 0 : l3.front();
        const int sibling_index
            = (int)(std::find(siblings.begin(), siblings.end(), cpu)
                    - siblings.begin());
        clusters[cluster].emplace_back(sibling_index, cpu);
        siblings_of[cpu] = std::move(siblings);
    }

    std::vector<std::vector<int>> ordered;
    for (auto& [key, cpus] : clusters)
    {
        std::sort(cpus.begin(), cpus.end());
        ordered.emplace_back();
        for (const auto& entry : cpus)
            ordered.back().push_back(entry.second);
    }

    opt.backend_cpu = ordered[0][0];
    const std::vector<int>& backend_core = siblings_of[opt.backend_cpu];

    if (opt.affinity == Affinity::Split && ordered.size() < 2)
        fail("split needs at least two CPU clusters (L3 caches), found one");

    const size_t first = opt.affinity == Affinity::Same ? 0 : 1;
    const size_t last = opt.affinity == Affinity::Same ? 1 : ordered.size();
    for (size_t c = first; c < last; ++c)
    {
        for (const int cpu : ordered[c])
        {
            if (std::find(backend_core.begin(), backend_core.end(), cpu)
                == backend_core.end())
                opt.frontend_cpus.push_back(cpu);
        }
    }
    if (opt.frontend_cpus.empty())
        fail("no CPUs left for the benchmark threads");

    std::string frontend;
    for (int t = 0; t < opt.threads; ++t)
    {
        frontend += " "
                    + std::to_string(
                        opt.frontend_cpus[t % opt.frontend_cpus.size()]);
    }
    std::fprintf(stderr,
                 "%s affinity: backend cpu %d, benchmark threads cpus%s%s\n",
                 opt.library,
                 opt.backend_cpu,
                 frontend.c_str(),
                 (size_t)opt.threads > opt.frontend_cpus.size()
                     ? " (some share a CPU)"
                     : "");
}

// Pins the calling thread to `cpu`, exits on failure
inline void pin_this_thread(int cpu)
{
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0)
        return;
#endif
    std::fprintf(stderr, "Failed to pin thread to cpu %d\n", cpu);
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
        else if (arg == "--affinity")
        {
            const std::string_view affinity = value;
            if (affinity == "none")
                opt.affinity = Affinity::None;
            else if (affinity == "same")
                opt.affinity = Affinity::Same;
            else if (affinity == "split")
                opt.affinity = Affinity::Split;
            else
                usage(argv[0]);
        }
        else
            usage(argv[0]);
    }

    if (opt.threads < 1 || opt.bursts < 1 || opt.burst_size < 1
        || opt.messages < opt.threads)
        usage(argv[0]);

    place_threads(opt);
    return opt;
}

// Runs body(thread_index) on opt.threads threads. All threads pin themselves
// (if enabled) and call init_thread() first, and then start the body at the
// same time.
// Returns the time the bodies were started
template <typename InitThread, typename Body>
Clock::time_point run_threads(const Options& opt, InitThread& init_thread,
                              Body body)
{
    const int n = opt.threads;
    std::latch ready{ n + 1 };
    std::vector<std::thread> threads;
    for (int t = 0; t < n; ++t)
    {
        threads.emplace_back([&, t] {
            // Pin before init_thread() so queues are allocated on the
            // thread's own CPU
            if (!opt.frontend_cpus.empty())
                pin_this_thread(
                    opt.frontend_cpus[t % opt.frontend_cpus.size()]);
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

    run_threads(opt, init_thread, [&](int t) {
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

    return run_threads(opt, init_thread, [&](int) {
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
