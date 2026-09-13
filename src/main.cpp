#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <iostream>
#include <string>
#include <thread>

#include "core.h"
#include "level.h"
#include "papper.h"
#include "queue.h"
#include "time.h"

int main()
{
    // Make the backend evict all Trace logs (in the trial loop)
    // We only care about frontend latency
    papper::set_level(papper::LogLevel::Info);

    double ns_per_tick;
    papper::time::get_tsc_ns_per_tick(ns_per_tick);
    papper::allocate(std::pow(2, 30));

    for (int i = 0; i < 1000; ++i)
    {
        PAPPER_LOG(papper::LogLevel::Trace, "warmup");
    }

    int trials = 1000000;
    uint64_t start = papper::time::read_tsc();
    for (int i = 0; i < trials; ++i)
    {
        PAPPER_LOG(papper::LogLevel::Trace, "hello");
    }
    uint64_t end = papper::time::read_tsc();

    double avg = ns_per_tick * (double)(end - start) / (double)trials;
    PAPPER_LOG(papper::LogLevel::Info, "average latency: {} ns ", avg);

    return 0;
}
