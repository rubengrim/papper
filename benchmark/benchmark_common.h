#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if (defined(__x86_64__) || defined(__i386__))
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#endif

namespace bench
{

inline uint64_t read_tsc()
{
#if (defined(__aarch64__))
    uint64_t count;
    __asm__ volatile("mrs \t%0, cntvct_el0" : "=r"(count));
    return count;
#elif (defined(__x86_64__) || defined(__i386__))
    return __rdtsc();
#else
    return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
}

class TimerCalibrator
{
  public:
    static TimerCalibrator& instance()
    {
        static TimerCalibrator inst;
        return inst;
    }

    double ns_per_tick() const
    {
        return _ns_per_tick;
    }

    double timer_overhead_ns() const
    {
        return _timer_overhead_ns;
    }

    double cycles_to_ns(uint64_t cycles) const
    {
        return cycles * _ns_per_tick;
    }

  private:
    TimerCalibrator()
    {
        calibrate();
    }

    void calibrate()
    {
        // 1. Calibrate TSC to nanoseconds by measuring over 50ms
        auto start = std::chrono::steady_clock::now();
        uint64_t start_tsc = read_tsc();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto end = std::chrono::steady_clock::now();
        uint64_t end_tsc = read_tsc();

        double elapsed_ns
            = std::chrono::duration<double, std::nano>(end - start).count();
        uint64_t elapsed_tsc = end_tsc - start_tsc;
        _ns_per_tick = elapsed_ns / static_cast<double>(elapsed_tsc);

        // 2. Measure median rdtsc() back-to-back timer overhead
        constexpr size_t N = 10000;
        std::vector<uint64_t> diffs(N);
        for (size_t i = 0; i < N; ++i)
        {
            uint64_t t0 = read_tsc();
            uint64_t t1 = read_tsc();
            diffs[i] = (t1 >= t0) ? (t1 - t0) : 0;
        }
        std::sort(diffs.begin(), diffs.end());
        _timer_overhead_ns = diffs[N / 2] * _ns_per_tick;
    }

    double _ns_per_tick = 1.0;
    double _timer_overhead_ns = 0.0;
};

struct LatencyStats
{
    std::string logger_name;
    std::string scenario_name;
    size_t iterations = 0;
    double batch_avg_ns = 0.0;
    double min_ns = 0.0;
    double p50_ns = 0.0;
    double p90_ns = 0.0;
    double p99_ns = 0.0;
    double p999_ns = 0.0;
    double max_ns = 0.0;
    double mops_per_sec = 0.0;

    static LatencyStats calculate(std::string_view logger,
                                 std::string_view scenario,
                                 double batch_time_sec,
                                 std::vector<uint64_t>& raw_cycles)
    {
        LatencyStats stats;
        stats.logger_name = logger;
        stats.scenario_name = scenario;
        stats.iterations = raw_cycles.size();

        if (stats.iterations == 0)
            return stats;

        // Batch average
        double total_batch_ns = batch_time_sec * 1e9;
        stats.batch_avg_ns = total_batch_ns / static_cast<double>(stats.iterations);
        stats.mops_per_sec = (batch_time_sec > 0)
                                 ? (static_cast<double>(stats.iterations) / (batch_time_sec * 1e6))
                                 : 0.0;

        // Sort cycles for percentiles
        std::sort(raw_cycles.begin(), raw_cycles.end());
        const double ns_per_tick = TimerCalibrator::instance().ns_per_tick();
        const double timer_overhead = TimerCalibrator::instance().timer_overhead_ns();

        auto get_percentile = [&](double pct) -> double {
            size_t idx = static_cast<size_t>(pct * (stats.iterations - 1));
            double ns = raw_cycles[idx] * ns_per_tick;
            return (ns > timer_overhead) ? (ns - timer_overhead) : 0.0;
        };

        stats.min_ns = std::max(0.0, raw_cycles.front() * ns_per_tick - timer_overhead);
        stats.p50_ns = get_percentile(0.50);
        stats.p90_ns = get_percentile(0.90);
        stats.p99_ns = get_percentile(0.99);
        stats.p999_ns = get_percentile(0.999);
        stats.max_ns = std::max(0.0, raw_cycles.back() * ns_per_tick - timer_overhead);

        return stats;
    }
};

enum class Scenario
{
    StaticString,
    SingleInt,
    TwoInts,
    SingleDouble,
    CString,
    StdString,
    MixedTypes,
    Count
};

inline std::string_view scenario_to_string(Scenario s)
{
    switch (s)
    {
    case Scenario::StaticString:
        return "Static String";
    case Scenario::SingleInt:
        return "Single Integer";
    case Scenario::TwoInts:
        return "Two Integers";
    case Scenario::SingleDouble:
        return "Single Double";
    case Scenario::CString:
        return "C-String Literal";
    case Scenario::StdString:
        return "std::string";
    case Scenario::MixedTypes:
        return "Mixed Types";
    default:
        return "Unknown";
    }
}

} // namespace bench
