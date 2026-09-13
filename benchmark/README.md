# Frontend Latency Benchmark: Papper vs fmtlog

This directory contains code for benchmarking the **frontend latency** of [Papper](..) against [fmtlog](https://github.com/MengRao/fmtlog).

## Focus: Frontend Latency

Frontend latency measures the elapsed time taken by a thread to call a logging statement (e.g. `info(...)` in Papper or `logi(...)` in fmtlog) and resume execution. Both loggers are asynchronous:
- The calling thread writes timestamp and argument metadata into a thread-local single-producer single-consumer (SPSC) ring buffer.
- Background threads process, format, and write log entries to the sink.

To measure purely the frontend overhead without disk I/O bottlenecks or queue drops:
- Both loggers use `/dev/null` sinks.
- Queues are configured to 64 MB to guarantee adequate capacity without queue stalls.
- Background drain threads run concurrently.
- Warmup cycles run prior to measurements to prime CPU caches, branch predictors, and TLS metadata.

## Metrics Reported

For each test scenario, the benchmark collects:
1. **Batch Average (ns)**: Measured over a tight loop of $N$ iterations using `std::chrono::steady_clock` to provide zero-overhead ground truth for average nanoseconds per call.
2. **Percentile Distribution**: Individual per-call latency measured via hardware timestamp counter (`rdtsc`), reporting:
   - **p50 (Median)**
   - **p90**
   - **p99**
   - **p99.9**
   - **Max**
3. **Throughput (M ops/s)**: Total logged operations per second.

## Scenarios Benchmarked

1. **Static String**: Message with no arguments.
2. **Single Integer**: Message with 1 integer argument.
3. **Two Integers**: Message with 2 integer arguments.
4. **Single Double**: Message with 1 floating-point argument.
5. **C-String Literal**: Message with a `const char*` pointer.
6. **std::string**: Message with a `std::string` argument (copying payload).
7. **Mixed Types**: Message with multiple types (`int`, `double`, `std::string`, `uint64_t`).

## Building

The benchmark is integrated into the CMake build system. Dependencies (`fmt` and `fmtlog`) are fetched automatically using CMake `FetchContent`.

```bash
# Configure in Release mode
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build the benchmark executable
cmake --build build --target benchmark_frontend
```

## Running

Run with default settings (100,000 iterations, 10,000 warmup iterations per scenario):

```bash
./build/benchmark/benchmark_frontend
```

### Command Line Options

```bash
# Custom iteration count and warmup
./build/benchmark/benchmark_frontend -i 200000 -w 20000

# Filter specific scenarios (e.g. integer)
./build/benchmark/benchmark_frontend --filter Integer

# Export results to CSV
./build/benchmark/benchmark_frontend --csv latency_results.csv
```
