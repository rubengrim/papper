#pragma once

#include "benchmark_common.h"

namespace bench::papper_runner
{

void init(size_t queue_size);
void drain();
LatencyStats run(Scenario scenario, size_t iters, size_t warmup_iters);

} // namespace bench::papper_runner
