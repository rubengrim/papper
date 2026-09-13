#pragma once

#include "benchmark_common.h"

namespace bench::fmtlog_runner
{

void init();
void drain();
void cleanup();
LatencyStats run(Scenario scenario, size_t iters, size_t warmup_iters);

} // namespace bench::fmtlog_runner
