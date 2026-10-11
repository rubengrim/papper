#!/usr/bin/env bash
# Builds the benchmarks in Release and runs latency and throughput benchmarks
# for papper, Quill and spdlog.
#
# Usage: benchmark/run_benchmarks.sh [extra benchmark args]
#   THREADS="1 4" sets the thread counts to run with (default "1 4")
#   Extra args are passed to every benchmark, e.g. --burst-size 1
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${script_dir}/build"
log_dir="${build_dir}/logs"
libraries=(papper quill spdlog)

cmake -S "${script_dir}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" --parallel
mkdir -p "${log_dir}"

for mode in latency throughput; do
    echo
    for threads in ${THREADS:-1 4}; do
        for library in "${libraries[@]}"; do
            "${build_dir}/bench_${library}" "${mode}" \
                --threads "${threads}" \
                --log-file "${log_dir}/${library}.log" \
                "$@"
        done
    done
done

rm -f "${log_dir}"/*.log
