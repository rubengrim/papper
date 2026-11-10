#pragma once

#include <string>
#include <string_view>

namespace papper
{

struct Color
{
    static constexpr const char* reset = "\033[0m";
    static constexpr const char* black = "\033[30m";
    static constexpr const char* red = "\033[31m";
    static constexpr const char* green = "\033[32m";
    static constexpr const char* yellow = "\033[33m";
    static constexpr const char* blue = "\033[34m";
    static constexpr const char* magenta = "\033[35m";
    static constexpr const char* cyan = "\033[36m";
    static constexpr const char* white = "\033[37m";
};

struct ColorConfig
{
    std::string trace{ Color::white };
    std::string debug{ Color::blue };
    std::string info{ Color::green };
    std::string warn{ Color::yellow };
    std::string error{ Color::red };
};

}
