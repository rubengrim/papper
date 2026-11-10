#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include "color.h"
#include "level.h"

namespace papper::detail
{

// https://blog.ganets.ky/StaticString/
template <size_t N>
struct StaticString
{
    static constexpr size_t size = N;
    std::array<char, N> data;

    constexpr StaticString(const char (&input)[N + 1])
    {
        std::copy_n(input, N, data.begin());
    }
};

template <std::size_t N>
StaticString(const char (&)[N]) -> StaticString<N - 1>;

struct MessageData
{
    LogLevel level;
    const char* thread_name;
    std::chrono::sys_time<std::chrono::nanoseconds> timestamp;
    const char* filename;
    const char* functionname;
    uint32_t linenumber;
    std::string_view message;
};

enum class PatternFieldType
{
    Level,
    ThreadName,
    Time,
    File,
    Function,
    Line,
    Message,
    Color,
};

enum class PatternFieldSpec
{
    None,
    TimeNS,       // Raw nanosecond timestamp
    TimeHMS,      // 16:08:09
    TimeHMSus,    // 16:08:09.796341
    TimeHMSns,    // 16:08:09.796341126
    FileFullPath, // /home/user/file.txt
    FileNameOnly, // file.txt
    ColorLevel,   // Will use configured log level color
    ColorReset,   // Marks end of color range
    ColorBlue,
};

struct PatternField
{
    PatternFieldType type;
    PatternFieldSpec spec;
};

enum class ParsingError
{
    None,
    UnterminatedBrace,
    EmptyFieldName,
    UnknownFieldType,
    InvalidFieldSpec,
    TooManyFields,
    UnmatchedClosingBrace,
};

// Given a type name and spec name, get the corresponding PatternField variant.
// Returns 0 on success, 1 if the field name is invalid, and 2 if the spec is
// invalid or can't be used for the specific field.
consteval std::optional<ParsingError>
get_and_validate_field(const std::string_view field_name,
                       const std::string_view spec, PatternField& field)
{
    if (field_name == "level")
    {
        field.type = PatternFieldType::Level;
        if (spec == "")
        {
            field.spec = PatternFieldSpec::None;
            return {};
        }
        return ParsingError::InvalidFieldSpec;
    }
    else if (field_name == "thread")
    {
        field.type = PatternFieldType::ThreadName;
        if (spec == "")
        {
            field.spec = PatternFieldSpec::None;
            return {};
        }
        return ParsingError::InvalidFieldSpec;
    }
    else if (field_name == "time")
    {
        field.type = PatternFieldType::Time;
        if (spec == "")
        {
            // Default to HMSus
            field.spec = PatternFieldSpec::TimeHMSus;
            return {};
        }
        else if (spec == "ns")
        {
            field.spec = PatternFieldSpec::TimeNS;
            return {};
        }
        else if (spec == "HMS")
        {
            field.spec = PatternFieldSpec::TimeHMS;
            return {};
        }
        else if (spec == "HMSus")
        {
            field.spec = PatternFieldSpec::TimeHMSus;
            return {};
        }
        else if (spec == "HMSns")
        {
            field.spec = PatternFieldSpec::TimeHMSns;
            return {};
        }
        return ParsingError::InvalidFieldSpec;
    }
    else if (field_name == "file")
    {
        field.type = PatternFieldType::File;
        if (spec == "")
        {
            field.spec = PatternFieldSpec::FileFullPath;
            return {};
        }
        else if (spec == "nameonly")
        {
            field.spec = PatternFieldSpec::FileNameOnly;
            return {};
        }
        return ParsingError::InvalidFieldSpec;
    }
    else if (field_name == "function")
    {
        field.type = PatternFieldType::Function;
        if (spec == "")
        {
            field.spec = PatternFieldSpec::None;
            return {};
        }
        return ParsingError::InvalidFieldSpec;
    }
    else if (field_name == "line")
    {
        field.type = PatternFieldType::Line;
        if (spec == "")
        {
            field.spec = PatternFieldSpec::None;
            return {};
        }
        return ParsingError::InvalidFieldSpec;
    }
    else if (field_name == "message")
    {
        field.type = PatternFieldType::Message;
        if (spec == "")
        {
            field.spec = PatternFieldSpec::None;
            return {};
        }
        return ParsingError::InvalidFieldSpec;
    }
    else if (field_name == "color")
    {
        field.type = PatternFieldType::Color;
        if (spec == "level")
        {
            field.spec = PatternFieldSpec::ColorLevel;
            return {};
        }
        else if (spec == "reset")
        {
            field.spec = PatternFieldSpec::ColorReset;
            return {};
        }
        else if (spec == "blue")
        {
            field.spec = PatternFieldSpec::ColorBlue;
            return {};
        }
        return ParsingError::InvalidFieldSpec;
    }

    return ParsingError::UnknownFieldType;
}

template <size_t MaxFmtStrLen, size_t MaxFields>
struct ParsedPattern
{
    constexpr std::string_view fmt_str_to_sv() const
    {
        return std::string_view(fmt_str, fmt_str_len);
    }

    ParsingError error = ParsingError::None;

    char fmt_str[MaxFmtStrLen];
    size_t fmt_str_len = 0;

    PatternField fields[MaxFields];
    size_t field_count = 0;
};

// Expand the field into it's representation in the result fmt str.
// For instance, {time:HMSus} will be expanded into {}.{:06}
// Others like {time:HMS} can be expanded into {} and don't need special
// handling
template <typename ParsedPatternT>
consteval void expand_field(PatternField field, ParsedPatternT& result)
{
    std::string_view expansion = "{}";

    if (field.spec == PatternFieldSpec::TimeHMSus)
    {
        expansion = "{}.{:06}";
    }
    else if (field.spec == PatternFieldSpec::TimeHMSns)
    {
        expansion = "{}.{:09}";
    }

    for (auto c : expansion)
    {
        result.fmt_str[result.fmt_str_len++] = c;
    }
}

template <StaticString Pattern, size_t MaxFields, size_t MaxFmtStrLen>
consteval auto parse_pattern()
{
    ParsedPattern<MaxFmtStrLen, MaxFields> result{};

    const std::size_t size = Pattern.size;
    std::size_t i = 0;

    auto push_char
        = [&result](char c) { result.fmt_str[result.fmt_str_len++] = c; };

    while (i < size)
    {
        char c = Pattern.data[i];

        if (c == '{')
        {
            // Check for escape sequence '{{'
            if (i + 1 < size && Pattern.data[i + 1] == '{')
            {
                // Forward escape sequence to fmt::vformat
                push_char('{');
                push_char('{');
                i += 2;
                continue;
            }

            // Try to find the matching closing bracket
            size_t bracket_close_idx = i + 1;
            while (bracket_close_idx < size
                   && Pattern.data[bracket_close_idx] != '}')
            {
                ++bracket_close_idx;
            }
            if (bracket_close_idx == size)
            {
                result.error = ParsingError::UnterminatedBrace;
                return result;
            }

            std::string_view name_and_spec(&Pattern.data[i + 1],
                                           bracket_close_idx - i - 1);

            if (name_and_spec.empty())
            {
                result.error = ParsingError::EmptyFieldName;
                return result;
            }

            std::string_view field_name = name_and_spec;
            std::string_view spec_name = "";

            size_t separator = name_and_spec.find_first_of(':');
            if (separator != std::string_view::npos)
            {
                field_name = name_and_spec.substr(0, separator);
                spec_name = name_and_spec.substr(separator + 1);
            }

            PatternField field;
            ParsingError err
                = get_and_validate_field(field_name, spec_name, field)
                      .value_or(ParsingError::None);
            if (err != ParsingError::None)
            {
                result.error = err;
                return result;
            }

            if (result.field_count >= MaxFields)
            {
                result.error = ParsingError::TooManyFields;
                return result;
            }

            result.fields[result.field_count] = field;
            result.field_count++;

            expand_field(field, result);

            i = bracket_close_idx + 1;
            continue;
        }

        if (c == '}')
        {
            // Check for escape sequence '}}' and forward it if found
            if (i + 1 < size && Pattern.data[i + 1] == '}')
            {
                push_char('}');
                push_char('}');
                i += 2;
                continue;
            }
            result.error = decltype(result.error)::UnmatchedClosingBrace;
            return result;
        }

        push_char(c);
        ++i;
    }

    return result;
}

// Updates the HMS part of the time arg only when it should change (around once
// a second), and calculates microsecond/nanosecond parts as needed
template <PatternField Field>
inline auto process_HMS_time_arg(
    const std::chrono::sys_time<std::chrono::nanoseconds>& arg)
{
    static constinit std::string HMS_part = "";
    static constinit auto last_update
        = std::chrono::sys_time<std::chrono::seconds>(
            std::chrono::seconds(-1));

    // Once the second-part of the arg doesn't match the second-part of the
    // last time we updated, update/reformat the HMS-part
    const auto arg_s = std::chrono::floor<std::chrono::seconds>(arg);
    if (arg_s != last_update)
    {
        HMS_part.clear();
        fmt::format_to(std::back_inserter(HMS_part), "{:%H:%M:%S}", arg_s);
        last_update = arg_s;
    }

    // Depending on spec, add the us or ns part to the output
    if constexpr (Field.spec == PatternFieldSpec::TimeHMS)
    {
        return std::make_tuple(HMS_part);
    }
    else if constexpr (Field.spec == PatternFieldSpec::TimeHMSus)
    {
        uint64_t us_part
            = std::chrono::floor<std::chrono::microseconds>(
                  (arg.time_since_epoch() % std::chrono::seconds(1)))
                  .count();
        return std::make_tuple(HMS_part, us_part);
    }
    else if constexpr (Field.spec == PatternFieldSpec::TimeHMSns)
    {
        uint64_t ns_part
            = (arg.time_since_epoch() % std::chrono::seconds(1)).count();
        return std::make_tuple(HMS_part, ns_part);
    }
}

// Some arguments need to be processed and/or expanded into multiple arguments,
// so do that here
template <PatternField Field, typename ArgType>
auto expand_and_process_arg(const ArgType& arg)
{
    if constexpr (Field.type == PatternFieldType::Time)
    {
        if constexpr (Field.spec == PatternFieldSpec::TimeNS)
        {
            // For the NS spec, just return the raw ns timestamp
            return std::make_tuple(arg.time_since_epoch().count());
        }
        else
        {
            return process_HMS_time_arg<Field>(arg);
        }
    }
    else if constexpr (Field.type == PatternFieldType::File
                       && Field.spec == PatternFieldSpec::FileNameOnly)
    {
        std::string_view path_sv = arg;
        std::string_view filename
            = path_sv.substr(path_sv.find_last_of("/\\") + 1);
        return std::make_tuple(filename);
    }
    else
    {
        // In most cases nothing needs to be done
        return std::make_tuple(arg);
    }
}

// TODO: Remove this function and just write a custom formatter for
// LogLevel
[[maybe_unused]] constexpr std::string_view level_to_name(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

[[maybe_unused]] static std::string_view
get_level_color(const ColorConfig& colors, LogLevel level)
{
    if (level == LogLevel::Trace)
        return colors.trace;
    else if (level == LogLevel::Debug)
        return colors.debug;
    else if (level == LogLevel::Info)
        return colors.info;
    else if (level == LogLevel::Warn)
        return colors.warn;
    else if (level == LogLevel::Error)
        return colors.error;
    else
        return "";
}

template <PatternField Field>
[[maybe_unused]] constexpr std::string_view
get_color(const ColorConfig& colors, LogLevel level, bool use_colors)
{
    if (!use_colors)
        return "";

    if constexpr (Field.spec == PatternFieldSpec::ColorLevel)
        return get_level_color(colors, level);
    else if constexpr (Field.spec == PatternFieldSpec::ColorReset)
        return Color::reset;
    else if constexpr (Field.spec == PatternFieldSpec::ColorBlue)
        return Color::blue;
    else
        static_assert(sizeof(LogLevel) == 0, "color spec is invalid");
}

// Note to me in the future: decltype(auto) combined with return ()
// adds a reference to the returned type, so pattern fields aren't
// actually copied
template <PatternField Field>
constexpr decltype(auto) get_field(const MessageData& data,
                                   const ColorConfig& colors, bool use_colors)
{
    if constexpr (Field.type == PatternFieldType::Level)
        return (level_to_name(data.level));
    else if constexpr (Field.type == PatternFieldType::Color)
        return (get_color<Field>(colors, data.level, use_colors));
    else if constexpr (Field.type == PatternFieldType::Time)
        return (data.timestamp);
    else if constexpr (Field.type == PatternFieldType::File)
        return (data.filename);
    else if constexpr (Field.type == PatternFieldType::Function)
        return (data.functionname);
    else if constexpr (Field.type == PatternFieldType::Line)
        return (data.linenumber);
    else if constexpr (Field.type == PatternFieldType::Message)
        return (data.message);
    else if constexpr (Field.type == PatternFieldType::ThreadName)
        return (data.thread_name);
    else
        static_assert(sizeof(int) == 0, "field is invalid");
}

// Need this for type erasure
class PatternFormatterBase
{
  public:
    virtual ~PatternFormatterBase() = default;
    virtual std::string format(const MessageData& data,
                               const ColorConfig& colors, bool use_colors) = 0;
};

template <StaticString Pattern, size_t MaxFields = 16,
          size_t MaxFmtStrLen = 1000> // 16 and 1000 should be enough
                                      // in basically all cases
class PatternFormatter : public PatternFormatterBase
{
  public:
    std::string format(const MessageData& data, const ColorConfig& colors,
                       bool use_colors) override
    {
        return format_impl(
            data,
            colors,
            use_colors,
            std::make_index_sequence<_parsed_pattern.field_count>{});
    }

  private:
    static constexpr ParsedPattern<MaxFmtStrLen, MaxFields> _parsed_pattern
        = parse_pattern<Pattern, MaxFields, MaxFmtStrLen>();

    // Asserts for nice error msgs
    static_assert(_parsed_pattern.error != ParsingError::UnterminatedBrace,
                  "pattern contains unterminated '{'");
    static_assert(_parsed_pattern.error != ParsingError::EmptyFieldName,
                  "pattern contains empty '{}'");
    static_assert(_parsed_pattern.error != ParsingError::UnknownFieldType,
                  "pattern contains unknown field type");
    static_assert(_parsed_pattern.error != ParsingError::InvalidFieldSpec,
                  "pattern contains unknown format spec or invalid field/spec "
                  "combination");
    static_assert(_parsed_pattern.error != ParsingError::TooManyFields,
                  "pattern contains too many fields (use "
                  "papper_set_long_pattern() instead)");
    static_assert(_parsed_pattern.error != ParsingError::UnmatchedClosingBrace,
                  "pattern contains '}' with no matching "
                  "opening brace '{'");

    template <std::size_t... I>
    std::string format_impl(const MessageData& data, const ColorConfig& colors,
                            bool use_colors, std::index_sequence<I...>)
    {
        constexpr std::string_view fmt_sv = _parsed_pattern.fmt_str_to_sv();

        auto args
            = std::tuple_cat(expand_and_process_arg<_parsed_pattern.fields[I]>(
                get_field<_parsed_pattern.fields[I]>(
                    data, colors, use_colors))...);

        return std::apply(
            [&fmt_sv](auto... x) {
                return fmt::vformat(fmt_sv, fmt::make_format_args(x...));
            },
            args);
    }
};

// Handles runtime switching of formatter and colors
class PatternFormatterHandler
{
  public:
    PatternFormatterHandler()
    {
        // Set default pattern and colors
        _formatter = new PatternFormatter<
            "{color:level}[{time:HMSus}] ({level}) "
            "({file:nameonly}:{line}) {message}{color:reset}">{};
        _colors = new ColorConfig{};
    }

    ~PatternFormatterHandler()
    {
        PatternFormatterBase* pending = _pending_new_formatter.exchange(
            nullptr, std::memory_order_acq_rel);
        if (pending != nullptr)
            delete pending;

        if (_formatter != nullptr)
            delete _formatter;
    }

    std::string format(const MessageData& data, bool use_colors)
    {
        return _formatter->format(data, *_colors, use_colors);
    }

    // Can be called by any thread
    void queue_new_pattern_formatter(PatternFormatterBase* formatter)
    {
        if (formatter == nullptr)
            return;
        PatternFormatterBase* prev = _pending_new_formatter.exchange(
            formatter, std::memory_order_acq_rel);
        if (prev != nullptr)
            delete prev;
    }

    // Can be called by any thread
    void queue_new_colors(const ColorConfig& colors)
    {
        ColorConfig* prev = _pending_new_colors.exchange(
            new ColorConfig{ colors }, std::memory_order_acq_rel);
        if (prev != nullptr)
            delete prev;
    }

    // Should only be called by backend
    void poll_for_pending_formatter_switch()
    {
        PatternFormatterBase* new_formatter = _pending_new_formatter.exchange(
            nullptr, std::memory_order_acq_rel);
        if (new_formatter != nullptr)
        {
            delete _formatter;
            _formatter = new_formatter;
        }
    }

    // Should only be called by backend
    void poll_for_pending_color_switch()
    {
        ColorConfig* new_colors
            = _pending_new_colors.exchange(nullptr, std::memory_order_acq_rel);
        if (new_colors != nullptr)
        {
            delete _colors;
            _colors = new_colors;
        }
    }

  private:
    PatternFormatterBase* _formatter;
    std::atomic<PatternFormatterBase*> _pending_new_formatter = nullptr;

    ColorConfig* _colors;
    std::atomic<ColorConfig*> _pending_new_colors = nullptr;
};
}
