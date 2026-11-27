#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <random>
#include <ratio>
#include <thread>
#include <unistd.h>
#include <vector>

#define PAPPER_MIN_LEVEL PAPPER_LEVEL_TRACE
#include "papper/detail/static_data_ref.h"
#include "papper/papper.h"

int main()
{
    const char* world = "world";
    PAPPER_TRACE("Hello {}", papper::detail::StaticDataRef<const char>(world));

    // std::random_device rd;
    // std::mt19937 gen(rd());
    // std::uniform_int_distribution<> distr(100, 2000);
    //
    // int i = 1;
    // while (true)
    // {
    //     TRACE("test message nr. {}", i++);
    //     int ms_wait = distr(gen);
    //     std::this_thread::sleep_for(std::chrono::milliseconds(ms_wait));
    // }
}
