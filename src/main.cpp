#include <chrono>
#include <iostream>
#include <thread>

#include "papper.h"

int main()
{

    papper::set_default_queue_size(10);
    papper::allocate(1000000);

    // std::this_thread::sleep_for(std::chrono::seconds(2));

    uint32_t i = 0;
    while (i++ < 10000)
    {
        papper::log("hej {}", std::to_string(i));
        // printf("hej %s\n", std::to_string(i).c_str());
    }

    return 0;
}
