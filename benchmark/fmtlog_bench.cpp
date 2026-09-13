#include "fmtlog_bench.h"

#include <fmtlog.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace bench::fmtlog_runner
{

void init()
{
    FILE* dev_null = std::fopen("/dev/null", "w");
    if (dev_null)
    {
        fmtlog::setLogFile(dev_null, false);
    }
    fmtlog::setHeaderPattern("[{l}] {m}");
    fmtlog::setLogLevel(fmtlog::DBG);
    fmtlog::setThreadName("fmtlog_bench");
    fmtlog::preallocate();
    // Poll every 1ms (1,000,000 ns) matching Papper's idle poll rate
    fmtlog::startPollingThread(1000000);
}

void drain()
{
    fmtlog::poll(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void cleanup()
{
    fmtlog::stopPollingThread();
}

namespace
{

template <typename Func>
LatencyStats run_bench_loop(Scenario scenario, Func&& log_fn, size_t iters, size_t warmup_iters)
{
    // 1. Warm-up
    for (size_t i = 0; i < warmup_iters; ++i)
    {
        log_fn(i);
    }
    drain();

    // 2. Batch latency (pure uninstrumented tight loop)
    auto t_start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < iters; ++i)
    {
        log_fn(i);
    }
    auto t_end = std::chrono::steady_clock::now();
    double batch_time_sec
        = std::chrono::duration<double>(t_end - t_start).count();

    drain();

    // 3. Sampled latency (per-call TSC timestamps)
    std::vector<uint64_t> raw_cycles(iters);
    for (size_t i = 0; i < iters; ++i)
    {
        uint64_t c0 = read_tsc();
        log_fn(i);
        uint64_t c1 = read_tsc();
        raw_cycles[i] = (c1 >= c0) ? (c1 - c0) : 0;
    }

    drain();

    return LatencyStats::calculate(
        "fmtlog", scenario_to_string(scenario), batch_time_sec, raw_cycles);
}

} // namespace

LatencyStats run(Scenario scenario, size_t iters, size_t warmup_iters)
{
    const std::string test_str = "benchmark_test_string";

    switch (scenario)
    {
    case Scenario::StaticString:
        return run_bench_loop(scenario, [](size_t) {
            logi("Static benchmark message without arguments.");
        }, iters, warmup_iters);

    case Scenario::SingleInt:
        return run_bench_loop(scenario, [](size_t i) {
            logi("Benchmark logging single integer: {}", static_cast<int>(i));
        }, iters, warmup_iters);

    case Scenario::TwoInts:
        return run_bench_loop(scenario, [](size_t i) {
            logi("Benchmark logging two integers: {} and {}", static_cast<int>(i), static_cast<int>(i * 2));
        }, iters, warmup_iters);

    case Scenario::SingleDouble:
        return run_bench_loop(scenario, [](size_t) {
            logi("Benchmark logging single double: {}", 3.141592653589793);
        }, iters, warmup_iters);

    case Scenario::CString:
        return run_bench_loop(scenario, [](size_t) {
            logi("Benchmark logging c-string: {}", "static_c_string_benchmark");
        }, iters, warmup_iters);

    case Scenario::StdString:
        return run_bench_loop(scenario, [&test_str](size_t) {
            logi("Benchmark logging std::string: {}", test_str);
        }, iters, warmup_iters);

    case Scenario::MixedTypes:
        return run_bench_loop(scenario, [&test_str](size_t i) {
            logi("Mixed types: iteration={}, value={:.3f}, tag={}, id={}",
                 static_cast<int>(i),
                 2.71828,
                 test_str,
                 9876543210ULL);
        }, iters, warmup_iters);

    default:
        return LatencyStats{};
    }
}

} // namespace bench::fmtlog_runner
