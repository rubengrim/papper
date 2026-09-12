#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <string>
#include <thread>

#include "papper.h"
#include "time.h"

int main()
{
    double ns_per_tick;
    papper::time::get_tsc_ns_per_tick(ns_per_tick);
    papper::allocate(std::pow(2, 28));

    for (int i = 0; i < 1000; ++i)
    {
        papper::info("warmup");
    }

    int trials = 1000000;
    uint64_t start = papper::time::read_tsc();
    for (int i = 0; i < trials; ++i)
    {
        papper::info("hello");
    }
    uint64_t end = papper::time::read_tsc();

    double avg = ns_per_tick * (double)(end - start) / (double)trials;
    papper::info("average latency: {} ns", avg);

    return 0;
}
