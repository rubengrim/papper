#include "papper/detail/color.h"
#include "papper/detail/core.h"
#include "papper/detail/sink.h"
#include "papper/detail/time.h"
#include "papper/papper.h"
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <utility>

int main()
{
    PAPPER_WARN("in terminal");

    // papper::disable_colors();
    // auto _ = papper::set_sink("test.log", true);
    papper::set_flush_behaviour(
        papper::FlushConfig{ .flush_mode = papper::FlushMode::Auto,
                             .buffer_size = 10000,
                             .max_flush_interval_ms = 1000 });

    int i = 0;
    while (i < 10000)
    {
        PAPPER_TRACE("msg {}", i);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        i++;
    }

    // PAPPER_DEBUG("msg");
    // PAPPER_INFO("msg");
    // PAPPER_WARN("msg");
    // PAPPER_ERROR("msg");
}
