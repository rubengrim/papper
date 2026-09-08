#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "papper.h"
#include "prefix.h"

int main()
{
    // int err
    //     = papper::set_sink("papper.log", true, papper::FlushOn::BufferFull);
    // if (err != 0)
    // {
    //     std::cout << "err" << std::endl;
    // }

    PAPPER_SET_PREFIX("[{timestamp}] ");

    uint32_t i = 10;
    while (i++ < 20)
    {
        papper::log("hej {}", std::to_string(i));
    }

    return 0;
}
