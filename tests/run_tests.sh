#!/usr/bin/env bash
# Configures, builds and runs all papper tests.
#
# Usage: tests/run_tests.sh [--sanitize thread|address] [extra ctest args]
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${script_dir}/build/default"
sanitize=""

if [[ "${1:-}" == "--sanitize" ]]; then
    sanitize="${2:?--sanitize requires an argument (thread|address)}"
    build_dir="${script_dir}/build/${sanitize}"
    shift 2
fi

cmake -S "${script_dir}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DPAPPER_SANITIZE="${sanitize}"
cmake --build "${build_dir}" --parallel
ctest --test-dir "${build_dir}" --output-on-failure "$@"
