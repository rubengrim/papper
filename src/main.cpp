#include <chrono>
#include <iostream>
#include <thread>

#include "papper.h"

int main()
{
    papper::set_sink("papper.log", false);
    // papper::set_sink(stdout);
    uint32_t i = 10;
    while (i++ < 20)
    {
        papper::log("hej {}", std::to_string(i));
    }

    return 0;
}
