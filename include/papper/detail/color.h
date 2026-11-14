#pragma once

#include <string>
#include <string_view>

namespace papper
{

// clang-format off

// https://www.geeksforgeeks.org/c/x-macros-in-c/
#define PAPPER_COLOR_CODES         \
    X(reset, "\033[0m")               \
                                      \
    /* Foregrounds */                 \
    X(black,             "\033[30m")  \
    X(red,               "\033[31m")  \
    X(green,             "\033[32m")  \
    X(yellow,            "\033[33m")  \
    X(blue,              "\033[34m")  \
    X(magenta,           "\033[35m")  \
    X(cyan,              "\033[36m")  \
    X(white,             "\033[37m")  \
    X(bright_black,      "\033[90m")  \
    X(bright_red,        "\033[91m")  \
    X(bright_green,      "\033[92m")  \
    X(bright_yellow,     "\033[93m")  \
    X(bright_blue,       "\033[94m")  \
    X(bright_magenta,    "\033[95m")  \
    X(bright_cyan,       "\033[96m")  \
    X(bright_white,      "\033[97m")  \
                                      \
    /* Backgrounds */                 \
    X(on_black,          "\033[40m")  \
    X(on_red,            "\033[41m")  \
    X(on_green,          "\033[42m")  \
    X(on_yellow,         "\033[43m")  \
    X(on_blue,           "\033[44m")  \
    X(on_magenta,        "\033[45m")  \
    X(on_cyan,           "\033[46m")  \
    X(on_white,          "\033[47m")  \
    X(on_bright_black,   "\033[100m") \
    X(on_bright_red,     "\033[101m") \
    X(on_bright_green,   "\033[102m") \
    X(on_bright_yellow,  "\033[103m") \
    X(on_bright_blue,    "\033[104m") \
    X(on_bright_magenta, "\033[105m") \
    X(on_bright_cyan,    "\033[106m") \
    X(on_bright_white,   "\033[107m") \
                                      \
    /* Modifiers */                   \
    X(bold,              "\033[1m")   \
    X(dark,              "\033[2m")   \
    X(italic,            "\033[3m")   \
    X(underline,         "\033[4m")   \
    X(blink,             "\033[5m")


// Holds color codes (the ones defined above) defined as static members,
// and also generates a {name, code} pair for every color, allowing access
// to a color code either by for example ColorCode::red, or by
// ColorCode::from_name("red")
struct ColorCode
{
// Create a member for each color
#define X(name, code) static constexpr std::string_view name = code;
PAPPER_COLOR_CODES
#undef X

static constexpr std::string_view from_name(std::string_view name)
{
    for (size_t i = 0; i < n_codes; ++i)
    {
        if (name_to_code[i].first == name)
        {
            return name_to_code[i].second;
        }
    }

    return "";
}

// Create an array of {name, code} pairs
#define X(name, code) { #name, code },
static constexpr std::pair<std::string_view, std::string_view> name_to_code[] {
    PAPPER_COLOR_CODES
};
#undef X

static constexpr size_t n_codes = sizeof(name_to_code) / sizeof(*name_to_code);
};

// clang-format on

struct Color
{
    // Takes any number of parameters that can be converted to string and
    // concatenates them into the final color code
    template <typename... Args>
    Color(Args... args)
    {
        color = (std::string(args) + ...);
    }

    std::string color;
};

struct ColorConfig
{
    Color trace{ ColorCode::white, ColorCode::dark };
    Color debug{ ColorCode::blue };
    Color info{ ColorCode::white };
    Color warn{ ColorCode::yellow };
    Color error{ ColorCode::bright_red, ColorCode::on_red };
};

}
