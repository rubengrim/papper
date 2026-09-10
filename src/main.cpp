#include <chrono>
#include <format>
#include <iostream>
#include <string>
#include <thread>

#include "papper.h"

int main()
{
    papper::set_level(papper::Level::Error);

    papper::trace("abcd");
    papper::debug("abcd");
    papper::info("abcd");
    papper::warn("abcd");
    papper::error("abcd");

    // std::string_view s = "/home/ruben.txt";
    // std::string_view s2 = s.substr(s.find_last_of("/\\") + 1);

    // papper::log("{}", s2);

    return 0;
}
