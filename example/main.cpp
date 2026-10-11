#include "papper/detail/time.h"
#include "papper/papper.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

papper::detail::Clock c;
double begin = 0;
size_t n_warmup = 10'000;
size_t n_messages = 2'000'000;

void finish()
{
    double elapsed_seconds = (c.read_timestamp_ns() - begin) * 1e-9;
    double msgs_per_second = n_messages / elapsed_seconds / 1e6;
    std::cout << msgs_per_second << " million msgs/s" << std::endl;
}

int main()
{
    std::atexit(finish);

    if (papper::set_sink(
            "test.log", true, papper::FlushOn::BufferFull, 64 * 1024)
        != 0)
        std::cout << "Fail" << std::endl;

    papper::set_default_queue_size((n_messages + n_warmup) * 128);
    papper::init_thread();

    c.init();

    // Warmup
    for (size_t i = 0; i < n_warmup; i++)
    {
        TRACE("message {}", i);
    }

    // std::this_thread::sleep_for(std::chrono::seconds(1));

    begin = c.read_timestamp_ns();
    for (size_t i = 0; i < n_messages; i++)
    {
        TRACE("message {}", i);
    }
}
