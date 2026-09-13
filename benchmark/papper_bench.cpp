#include "papper_bench.h"

#include <papper/papper.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace bench::papper_runner
{

void init(size_t queue_size)
{
    FILE* dev_null = std::fopen("/dev/null", "w");
    if (dev_null)
    {
        papper::set_sink(dev_null);
    }
    papper_set_pattern("[{level}] {message}");
    papper::set_level(papper::LogLevel::Trace);
    papper::init_thread("papper_bench", queue_size);
}

void drain()
{
    // Sleep briefly to let the background thread drain the queue
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

namespace
{

inline void execute_log(Scenario scenario, size_t i, const std::string& str)
{
    switch (scenario)
    {
    case Scenario::StaticString:
        info("Static benchmark message without arguments.");
        break;
    case Scenario::SingleInt:
        info("Benchmark logging single integer: {}", static_cast<int>(i));
        break;
    case Scenario::TwoInts:
        info("Benchmark logging two integers: {} and {}",
             static_cast<int>(i),
             static_cast<int>(i * 2));
        break;
    case Scenario::SingleDouble:
        info("Benchmark logging single double: {}", 3.141592653589793);
        break;
    case Scenario::CString:
        info("Benchmark logging c-string: {}", "static_c_string_benchmark");
        break;
    case Scenario::StdString:
        info("Benchmark logging std::string: {}", str);
        break;
    case Scenario::MixedTypes:
        info("Mixed types: iteration={}, value={:.3f}, tag={}, id={}",
             static_cast<int>(i),
             2.71828,
             str,
             9876543210ULL);
        break;
    default:
        break;
    }
}

} // namespace

LatencyStats run(Scenario scenario, size_t iters, size_t warmup_iters)
{
    const std::string test_str = "benchmark_test_string";

    // 1. Warm-up
    for (size_t i = 0; i < warmup_iters; ++i)
    {
        execute_log(scenario, i, test_str);
    }
    drain();

    // 2. Batch latency (pure uninstrumented loop)
    auto t_start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < iters; ++i)
    {
        execute_log(scenario, i, test_str);
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
        execute_log(scenario, i, test_str);
        uint64_t c1 = read_tsc();
        raw_cycles[i] = (c1 >= c0) ? (c1 - c0) : 0;
    }

    drain();

    return LatencyStats::calculate(
        "Papper", scenario_to_string(scenario), batch_time_sec, raw_cycles);
}

} // namespace bench::papper_runner
