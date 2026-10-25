#include <doctest/doctest.h>

#include <chrono>
#include <string>

#include "papper/detail/pattern.h"

using namespace papper::detail;
using papper::LogLevel;

namespace
{

// 16:08:09.796341126 on some day
const auto test_timestamp = std::chrono::sys_days{ std::chrono::year{ 2024 }
                                                   / 3 / 14 }
                            + std::chrono::hours{ 16 }
                            + std::chrono::minutes{ 8 }
                            + std::chrono::seconds{ 9 }
                            + std::chrono::nanoseconds{ 796341126 };

PatternData make_data(std::string_view message = "hello")
{
    return PatternData{
        .level = LogLevel::Warn,
        .thread_name = "worker",
        .timestamp = test_timestamp,
        .filename = "/home/user/project/src/file.cpp",
        .functionname = "void do_stuff()",
        .linenumber = 123,
        .message = message,
    };
}

template <StaticString Pattern>
std::string format(const PatternData& data = make_data(),
                   const papper::ColorConfiguration* colors = nullptr)
{
    return PatternFormatter<Pattern>{}.format(data, colors);
}

template <StaticString Pattern>
constexpr ParsingError parse_error()
{
    return parse_pattern<Pattern, 4, 1000>().error;
}

} // namespace

TEST_CASE("pattern: plain fields")
{
    CHECK(format<"{message}">() == "hello");
    CHECK(format<"{level}">() == "WARN");
    CHECK(format<"{thread}">() == "worker");
    CHECK(format<"{line}">() == "123");
    CHECK(format<"{function}">() == "void do_stuff()");
    CHECK(format<"{file}">() == "/home/user/project/src/file.cpp");
}

TEST_CASE("pattern: literal text and multiple fields")
{
    CHECK(format<"">() == "");
    CHECK(format<"no fields">() == "no fields");
    CHECK(format<"<{level}> [{thread}] {file:nameonly}:{line} - {message}!">()
          == "<WARN> [worker] file.cpp:123 - hello!");
    CHECK(format<"{message}{message}">() == "hellohello");
}

TEST_CASE("pattern: all levels have names")
{
    auto data = make_data();
    const std::pair<LogLevel, const char*> expected[] = {
        { LogLevel::Trace, "TRACE" }, { LogLevel::Debug, "DEBUG" },
        { LogLevel::Info, "INFO" },   { LogLevel::Warn, "WARN" },
        { LogLevel::Error, "ERROR" },
    };
    for (const auto& [level, name] : expected)
    {
        data.level = level;
        CHECK(format<"{level}">(data) == name);
    }
}

TEST_CASE("pattern: escaped braces are emitted literally")
{
    CHECK(format<"{{{level}}}">() == "{WARN}");
    CHECK(format<"{{message}}">() == "{message}");
}

TEST_CASE("pattern: braces inside the message are not interpreted")
{
    CHECK(format<"{message}">(make_data("{} {0} }{")) == "{} {0} }{");
}

TEST_CASE("pattern: file name only strips directories")
{
    auto data = make_data();
    CHECK(format<"{file:nameonly}">(data) == "file.cpp");

    data.filename = "C:\\src\\win.cpp";
    CHECK(format<"{file:nameonly}">(data) == "win.cpp");

    data.filename = "bare.cpp";
    CHECK(format<"{file:nameonly}">(data) == "bare.cpp");
}

TEST_CASE("pattern: time precision specs")
{
    CHECK(format<"{time:HMS}">() == "16:08:09");
    CHECK(format<"{time:HMSf}">() == "16:08:09.796341");
    CHECK(format<"{time:HMSF}">() == "16:08:09.796341126");
}

TEST_CASE("pattern: invalid patterns are rejected at compile time")
{
    static_assert(parse_error<"{level} {message}">() == ParsingError::None);
    static_assert(parse_error<"{level">() == ParsingError::UnterminatedBrace);
    static_assert(parse_error<"{}">() == ParsingError::EmptyFieldName);
    static_assert(parse_error<"{nope}">() == ParsingError::UnknownFieldType);
    static_assert(parse_error<"{time:nope}">()
                  == ParsingError::InvalidFieldSpec);
    static_assert(parse_error<"{level:HMS}">()
                  == ParsingError::InvalidFieldSpec);
    static_assert(parse_error<"{line:nameonly}">()
                  == ParsingError::InvalidFieldSpec);
    static_assert(parse_error<"{color}">() == ParsingError::InvalidFieldSpec);
    static_assert(parse_error<"{color:nope}">()
                  == ParsingError::InvalidFieldSpec);
    static_assert(parse_error<"{color:level}{color:reset}{color:blue}">()
                  == ParsingError::None);
    static_assert(
        parse_error<"{message}{message}{message}{message}{message}">()
        == ParsingError::TooManyFields);
    static_assert(parse_error<"level}">()
                  == ParsingError::UnmatchedClosingBrace);
}

TEST_CASE("pattern: color fields are empty without colors")
{
    CHECK(format<"{color:level}{level}{color:reset} {color:blue}{line}">()
          == "WARN 123");
}

TEST_CASE("pattern: color fields use the configured colors")
{
    const papper::ColorConfiguration colors{ .warn = "<warn>" };
    CHECK(format<"{color:level}{level}{color:reset} {color:blue}{line}">(
              make_data(), &colors)
          == std::string("<warn>WARN") + papper::Color::reset + " "
                 + papper::Color::blue + "123");

    auto data = make_data();
    data.level = LogLevel::Error;
    CHECK(format<"{color:level}{level}">(data, &colors)
          == std::string(papper::Color::red) + "ERROR");
}
