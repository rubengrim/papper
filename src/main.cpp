#include <chrono>
#include <format>
#include <iostream>
#include <string>
#include <thread>

#include "papper.h"
#include "prefix.h"

int main()
{
    PAPPER_SET_PREFIX("[{timestamp:HMSf}] ");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    uint32_t i = 10;
    while (i++ < 20)
    {
        papper::log("hej {}", std::to_string(i));
    }

    // auto now = std::chrono::system_clock::now();
    // auto local_now = std::chrono::current_zone()->to_local(now);
    // auto local_now_us
    //     =
    //     std::chrono::time_point_cast<std::chrono::microseconds>(local_now);
    // std::cout << std::format("{:%H:%M:%S}", local_now) << std::endl;
    // std::cout << std::format("{:%H:%M:%S}", local_now_us) << std::endl;

    return 0;
}
