#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#include "papper/papper.h"

int main()
{
    papper::init_thread(33);

    for (int i = 0; i < 10000; i++)
    {
        WARN("blocking {}", i);
    }

    // PAPPER_TRACE("msg");
    // PAPPER_DEBUG("msg");
    // PAPPER_INFO("msg");
    // PAPPER_WARN("msg");
    // PAPPER_ERROR("msg");
}
