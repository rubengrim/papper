#include "benchmark_common.h"
#include "fmtlog_bench.h"
#include "papper_bench.h"

#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{

struct BenchmarkConfig
{
    size_t iterations = 100000;
    size_t warmup_iters = 10000;
    size_t queue_size = 64 * 1024 * 1024; // 64 MB
    std::string csv_path = "";
    std::string filter = "";
};

void print_help(const char* prog)
{
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -i, --iterations <N>   Number of iterations per scenario (default: 100000)\n"
              << "  -w, --warmup <N>       Number of warmup iterations (default: 10000)\n"
              << "  -q, --queue-size <MB>  Queue size in MB (default: 64)\n"
              << "  -c, --csv <file>       Export results to CSV file\n"
              << "  -f, --filter <str>     Filter scenarios by substring\n"
              << "  -h, --help             Show this help message\n";
}

bool parse_args(int argc, char** argv, BenchmarkConfig& config)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help")
        {
            print_help(argv[0]);
            return false;
        }
        else if ((arg == "-i" || arg == "--iterations") && i + 1 < argc)
        {
            config.iterations = std::stoull(argv[++i]);
        }
        else if ((arg == "-w" || arg == "--warmup") && i + 1 < argc)
        {
            config.warmup_iters = std::stoull(argv[++i]);
        }
        else if ((arg == "-q" || arg == "--queue-size") && i + 1 < argc)
        {
            config.queue_size = std::stoull(argv[++i]) * 1024 * 1024;
        }
        else if ((arg == "-c" || arg == "--csv") && i + 1 < argc)
        {
            config.csv_path = argv[++i];
        }
        else if ((arg == "-f" || arg == "--filter") && i + 1 < argc)
        {
            config.filter = argv[++i];
        }
        else
        {
            std::cerr << "Unknown or invalid option: " << arg << "\n";
            print_help(argv[0]);
            return false;
        }
    }
    return true;
}

void print_row(std::string_view scenario,
               std::string_view logger,
               double batch_avg,
               double p50,
               double p90,
               double p99,
               double p999,
               double max_val,
               double mops)
{
    std::cout << "| " << std::left << std::setw(18) << scenario
              << "| " << std::left << std::setw(8) << logger
              << "| " << std::right << std::fixed << std::setprecision(1) << std::setw(9) << batch_avg
              << " | " << std::right << std::setw(8) << p50
              << " | " << std::right << std::setw(8) << p90
              << " | " << std::right << std::setw(8) << p99
              << " | " << std::right << std::setw(8) << p999
              << " | " << std::right << std::setw(9) << max_val
              << " | " << std::right << std::setprecision(2) << std::setw(8) << mops
              << " |\n";
}

void print_separator()
{
    std::cout << "+-------------------+---------+-----------+----------+----------+----------+----------+-----------+----------+\n";
}

void print_header()
{
    print_separator();
    std::cout << "| Scenario          | Logger  | Avg (ns)  | p50 (ns) | p90 (ns) | p99 (ns) | p99.9(ns)| Max (ns)  | M ops/s  |\n";
    print_separator();
}

} // namespace

int main(int argc, char** argv)
{
    BenchmarkConfig config;
    if (!parse_args(argc, argv, config))
    {
        return 0;
    }

    auto& calibrator = bench::TimerCalibrator::instance();

    std::cout << "=========================================================================================\n"
              << "                PAPPER vs FMTLOG - FRONTEND LATENCY BENCHMARK                            \n"
              << "=========================================================================================\n"
              << "Focus: Frontend logging call latency (nanoseconds per call)\n"
              << "Iterations per scenario: " << config.iterations << "\n"
              << "Warmup iterations:       " << config.warmup_iters << "\n"
              << "Queue capacity:          " << (config.queue_size / (1024 * 1024)) << " MB\n"
              << "Calibrated TSC:          " << std::fixed << std::setprecision(4)
              << (1.0 / calibrator.ns_per_tick()) << " GHz (" << calibrator.ns_per_tick() << " ns/tick)\n"
              << "Timer measurement floor: " << std::setprecision(1) << calibrator.timer_overhead_ns() << " ns\n"
              << "=========================================================================================\n\n";

    std::cout << "Initializing loggers with /dev/null sinks and background drain threads...\n";
    bench::papper_runner::init(config.queue_size);
    bench::fmtlog_runner::init();

    // Small stabilization pause
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<bench::Scenario> scenarios = {
        bench::Scenario::StaticString,
        bench::Scenario::SingleInt,
        bench::Scenario::TwoInts,
        bench::Scenario::SingleDouble,
        bench::Scenario::CString,
        bench::Scenario::StdString,
        bench::Scenario::MixedTypes,
    };

    std::vector<bench::LatencyStats> all_results;

    std::cout << "Running benchmarks...\n\n";
    print_header();

    for (auto scenario : scenarios)
    {
        std::string sc_name = std::string(bench::scenario_to_string(scenario));
        if (!config.filter.empty() && sc_name.find(config.filter) == std::string::npos)
        {
            continue;
        }

        auto papper_stats = bench::papper_runner::run(scenario, config.iterations, config.warmup_iters);
        auto fmtlog_stats = bench::fmtlog_runner::run(scenario, config.iterations, config.warmup_iters);

        print_row(sc_name,
                  papper_stats.logger_name,
                  papper_stats.batch_avg_ns,
                  papper_stats.p50_ns,
                  papper_stats.p90_ns,
                  papper_stats.p99_ns,
                  papper_stats.p999_ns,
                  papper_stats.max_ns,
                  papper_stats.mops_per_sec);

        print_row(sc_name,
                  fmtlog_stats.logger_name,
                  fmtlog_stats.batch_avg_ns,
                  fmtlog_stats.p50_ns,
                  fmtlog_stats.p90_ns,
                  fmtlog_stats.p99_ns,
                  fmtlog_stats.p999_ns,
                  fmtlog_stats.max_ns,
                  fmtlog_stats.mops_per_sec);

        print_separator();

        all_results.push_back(papper_stats);
        all_results.push_back(fmtlog_stats);
    }

    // Comparison summary
    std::cout << "\n=========================================================================================\n"
              << "                                PERFORMANCE COMPARISON                                    \n"
              << "=========================================================================================\n";
    for (size_t i = 0; i + 1 < all_results.size(); i += 2)
    {
        const auto& p = all_results[i];
        const auto& f = all_results[i + 1];

        double avg_diff_pct = ((p.batch_avg_ns - f.batch_avg_ns) / f.batch_avg_ns) * 100.0;
        double p50_diff_pct = ((p.p50_ns - f.p50_ns) / f.p50_ns) * 100.0;

        std::cout << std::left << std::setw(20) << p.scenario_name << ": ";
        if (std::abs(avg_diff_pct) < 5.0)
        {
            std::cout << "Comparable latency (Papper: " << std::fixed << std::setprecision(1)
                      << p.batch_avg_ns << " ns vs fmtlog: " << f.batch_avg_ns << " ns)\n";
        }
        else if (avg_diff_pct < 0)
        {
            std::cout << "Papper is " << std::fixed << std::setprecision(1) << -avg_diff_pct
                      << "% FASTER on avg (" << p.batch_avg_ns << " ns vs " << f.batch_avg_ns << " ns)\n";
        }
        else
        {
            std::cout << "fmtlog is " << std::fixed << std::setprecision(1) << avg_diff_pct
                      << "% FASTER on avg (" << f.batch_avg_ns << " ns vs " << p.batch_avg_ns << " ns)\n";
        }
    }
    std::cout << "=========================================================================================\n";

    // Export CSV if requested
    if (!config.csv_path.empty())
    {
        std::ofstream csv(config.csv_path);
        if (csv.is_open())
        {
            csv << "Scenario,Logger,Iterations,BatchAvgNs,MinNs,P50Ns,P90Ns,P99Ns,P999Ns,MaxNs,MopsPerSec\n";
            for (const auto& r : all_results)
            {
                csv << "\"" << r.scenario_name << "\","
                    << "\"" << r.logger_name << "\","
                    << r.iterations << ","
                    << r.batch_avg_ns << ","
                    << r.min_ns << ","
                    << r.p50_ns << ","
                    << r.p90_ns << ","
                    << r.p99_ns << ","
                    << r.p999_ns << ","
                    << r.max_ns << ","
                    << r.mops_per_sec << "\n";
            }
            std::cout << "\nResults exported to: " << config.csv_path << "\n";
        }
        else
        {
            std::cerr << "Failed to open CSV file for writing: " << config.csv_path << "\n";
        }
    }

    bench::fmtlog_runner::cleanup();

    return 0;
}
