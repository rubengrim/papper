#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#include "papper/papper.h"

int main()
{
    PAPPER_WARN("in terminal");

    // papper::disable_colors();
    // auto _ = papper::set_sink("test.log", true);
    // papper::set_flush_behaviour(
    //     papper::FlushConfig{ .flush_mode = papper::FlushMode::Auto,
    //                          .buffer_size = 10000,
    //                          .max_flush_interval_ms = 1000 });

    PAPPER_TRACE("msg");
    PAPPER_DEBUG("msg");
    papper::set_sink("text.log");
    PAPPER_INFO("msg");
    PAPPER_WARN("msg");
    PAPPER_ERROR("msg");
}
