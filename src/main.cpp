#include <chrono>
#include <format>
#include <iostream>
#include <string>
#include <thread>

#include "papper.h"
#include "prefix.h"

int main()
{
    PAPPER_SET_PREFIX("[{time:HMSf}] {file:nameonly}:{line} ");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    uint32_t i = 10;
    while (i++ < 20)
    {
        papper::log("hej {}", std::to_string(i));
    }

    // std::string_view s = "/home/ruben.txt";
    // std::string_view s2 = s.substr(s.find_last_of("/\\") + 1);

    // papper::log("{}", s2);

    return 0;
}
