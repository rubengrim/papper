#pragma once

#include <string>
#include <string_view>

#include "level.h"

namespace papper
{

// ANSI escape codes
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

// Colors used for {color:level} in the pattern. Any escape sequence can be
// used, not only the ones in Color
struct ColorConfiguration
{
    std::string trace = Color::white;
    std::string debug = Color::blue;
    std::string info = Color::green;
    std::string warn = Color::yellow;
    std::string error = Color::red;

    std::string_view level_color(const LogLevel level) const
    {
        switch (level)
        {
        case LogLevel::Trace:
            return trace;
        case LogLevel::Debug:
            return debug;
        case LogLevel::Info:
            return info;
        case LogLevel::Warn:
            return warn;
        case LogLevel::Error:
            return error;
        }
        return "";
    }
};

enum class ColorMode
{
    Auto, // Colored output only for console output
    Always,
    Never,
};

} // end namespace papper
