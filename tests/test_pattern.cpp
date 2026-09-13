#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <papper/detail/pattern.h>

#include <chrono>
#include <string>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;

TEST_CASE("StaticString compile-time properties", "[pattern]")
{
    constexpr papper::detail::StaticString str("hello world");
    STATIC_REQUIRE(str.size == 11);
    REQUIRE(std::string_view(str.data.data(), str.size) == "hello world");
}

TEST_CASE("level_to_name conversion", "[pattern]")
{
    using papper::LogLevel;
    using papper::detail::level_to_name;

    REQUIRE(level_to_name(LogLevel::Trace) == "TRACE");
    REQUIRE(level_to_name(LogLevel::Debug) == "DEBUG");
    REQUIRE(level_to_name(LogLevel::Info) == "INFO");
    REQUIRE(level_to_name(LogLevel::Warn) == "WARN");
    REQUIRE(level_to_name(LogLevel::Error) == "ERROR");
}

TEST_CASE("Pattern field and spec name parsing", "[pattern]")
{
    using namespace papper::detail;

    SECTION("Field name mapping")
    {
        PatternField field;
        REQUIRE(name_to_field_enum("time", field));
        REQUIRE(field == PatternField::Time);

        REQUIRE(name_to_field_enum("file", field));
        REQUIRE(field == PatternField::File);

        REQUIRE(name_to_field_enum("function", field));
        REQUIRE(field == PatternField::Function);

        REQUIRE(name_to_field_enum("line", field));
        REQUIRE(field == PatternField::Line);

        REQUIRE(name_to_field_enum("level", field));
        REQUIRE(field == PatternField::Level);

        REQUIRE(name_to_field_enum("message", field));
        REQUIRE(field == PatternField::Message);

        REQUIRE(name_to_field_enum("m", field));
        REQUIRE(field == PatternField::Message);

        REQUIRE(name_to_field_enum("thread", field));
        REQUIRE(field == PatternField::ThreadName);

        REQUIRE_FALSE(name_to_field_enum("unknown_field", field));
    }

    SECTION("Format spec mapping")
    {
        PatternFieldFormatSpec spec;
        REQUIRE(name_to_format_spec_enum("", spec));
        REQUIRE(spec == PatternFieldFormatSpec::None);

        REQUIRE(name_to_format_spec_enum("HMS", spec));
        REQUIRE(spec == PatternFieldFormatSpec::TimeHMS);

        REQUIRE(name_to_format_spec_enum("HMSf", spec));
        REQUIRE(spec == PatternFieldFormatSpec::TimeHMSf);

        REQUIRE(name_to_format_spec_enum("HMSF", spec));
        REQUIRE(spec == PatternFieldFormatSpec::TimeHMSF);

        REQUIRE(name_to_format_spec_enum("path", spec));
        REQUIRE(spec == PatternFieldFormatSpec::None);

        REQUIRE(name_to_format_spec_enum("nameonly", spec));
        REQUIRE(spec == PatternFieldFormatSpec::FileNameOnly);

        REQUIRE_FALSE(name_to_format_spec_enum("invalid_spec", spec));
    }

    SECTION("Field and spec compatibility validation")
    {
        REQUIRE(validate_field_spec_combination(
            PatternField::Time, PatternFieldFormatSpec::TimeHMS));
        REQUIRE(validate_field_spec_combination(
            PatternField::Time, PatternFieldFormatSpec::TimeHMSf));
        REQUIRE(validate_field_spec_combination(
            PatternField::Time, PatternFieldFormatSpec::TimeHMSF));
        REQUIRE(validate_field_spec_combination(
            PatternField::File, PatternFieldFormatSpec::FileNameOnly));
        REQUIRE(validate_field_spec_combination(PatternField::Level,
                                                PatternFieldFormatSpec::None));

        // Invalid combinations
        REQUIRE_FALSE(validate_field_spec_combination(
            PatternField::Level, PatternFieldFormatSpec::TimeHMS));
        REQUIRE_FALSE(validate_field_spec_combination(
            PatternField::Line, PatternFieldFormatSpec::FileNameOnly));
    }
}

TEST_CASE("parse_pattern error detection", "[pattern]")
{
    using namespace papper::detail;

    SECTION("Unterminated brace")
    {
        constexpr auto res = parse_pattern<"hello {world", 16, 100>();
        STATIC_REQUIRE(res.error == ParsingError::UnterminatedBrace);
    }

    SECTION("Empty field name")
    {
        constexpr auto res = parse_pattern<"hello {}", 16, 100>();
        STATIC_REQUIRE(res.error == ParsingError::EmptyFieldName);
    }

    SECTION("Unknown field name")
    {
        constexpr auto res = parse_pattern<"hello {foo}", 16, 100>();
        STATIC_REQUIRE(res.error == ParsingError::UnknownFieldName);
    }

    SECTION("Unknown format specifier")
    {
        constexpr auto res = parse_pattern<"hello {time:xyz}", 16, 100>();
        STATIC_REQUIRE(res.error == ParsingError::UnknownFormatSpec);
    }

    SECTION("Invalid spec usage")
    {
        constexpr auto res = parse_pattern<"hello {line:nameonly}", 16, 100>();
        STATIC_REQUIRE(res.error == ParsingError::InvalidSpecUsage);
    }

    SECTION("Unmatched closing brace")
    {
        constexpr auto res = parse_pattern<"hello } world", 16, 100>();
        STATIC_REQUIRE(res.error == ParsingError::UnmatchedClosingBrace);
    }

    SECTION("Too many fields")
    {
        constexpr auto res
            = parse_pattern<"{level} {message} {line}", 2, 100>();
        STATIC_REQUIRE(res.error == ParsingError::TooManyFields);
    }

    SECTION("Escaped braces do not trigger errors")
    {
        constexpr auto res
            = parse_pattern<"{{escaped}} [{level}] {message} {{end}}",
                            16,
                            100>();
        STATIC_REQUIRE(res.error == ParsingError::None);
        STATIC_REQUIRE(res.field_count == 2);
    }
}

TEST_CASE("PatternFormatter string output", "[pattern]")
{
    using namespace papper::detail;

    PatternData data{
        .level = papper::LogLevel::Info,
        .thread_name = "test_thread",
        .timestamp = std::chrono::system_clock::now(),
        .filename = "/home/user/project/src/core.cpp",
        .functionname = "init_engine",
        .linenumber = 142,
        .message = "System initialized successfully",
    };

    SECTION("Level, thread and message")
    {
        PatternFormatter<"[{level}] ({thread}) {message}"> formatter;
        std::string result = formatter.format(data);
        REQUIRE(result
                == "[INFO] (test_thread) System initialized successfully");
    }

    SECTION("File with nameonly and line")
    {
        PatternFormatter<"{file:nameonly}:{line} {message}"> formatter;
        std::string result = formatter.format(data);
        REQUIRE(result == "core.cpp:142 System initialized successfully");
    }

    SECTION("Full path file and function")
    {
        PatternFormatter<"{file} in {function}(): {message}"> formatter;
        std::string result = formatter.format(data);
        REQUIRE(result
                == "/home/user/project/src/core.cpp in init_engine(): System "
                   "initialized successfully");
    }

    SECTION("Escaped braces in pattern")
    {
        PatternFormatter<"{{JSON: [\"{level}\", \"{message}\"]}}"> formatter;
        std::string result = formatter.format(data);
        REQUIRE(result
                == "{JSON: [\"INFO\", \"System initialized successfully\"]}");
    }
}

TEST_CASE("PatternFormatterHandler pattern switching", "[pattern]")
{
    using namespace papper::detail;

    PatternFormatterHandler handler;

    PatternData data{
        .level = papper::LogLevel::Warn,
        .thread_name = "worker",
        .timestamp = std::chrono::system_clock::now(),
        .filename = "main.cpp",
        .functionname = "main",
        .linenumber = 10,
        .message = "warning message",
    };

    // Default formatter contains level and message
    std::string default_out = handler.format(data);
    REQUIRE_THAT(default_out, ContainsSubstring("WARN"));
    REQUIRE_THAT(default_out, ContainsSubstring("warning message"));

    // Queue new formatter
    handler.queue_new_formatter(
        new PatternFormatter<"CUSTOM [{level}] {message}">);

    // Switch formatter
    handler.poll_for_pending_formatter_switch();

    std::string custom_out = handler.format(data);
    REQUIRE(custom_out == "CUSTOM [WARN] warning message");
}
