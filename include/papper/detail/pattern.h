#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
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
    FileNameOnly, // file.txt
    ColorLevel,   // Will use configured log level color
    ColorReset,
    ColorCustom,
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
    UnmatchedClosingBrace,
    EmptyField,
    UnknownFieldType,
    InvalidFieldSpec,
    InvalidFieldSyntax,
    TooManyFields,
    ExceedsMaxFmtStrLen,
};

consteval bool validate_custom_color_spec(std::string_view color_spec)
{
    for (auto name_it : std::views::split(color_spec, ','))
    {
        std::string_view name = { name_it.begin(), name_it.end() };
        // Check if the color doesn't exist
        if (ColorCode::from_name(name) == "")
        {
            return false;
        }
    }

    return true;
}

// Given a type name and spec name, get the corresponding PatternField variant
// Optionally returns a ParsingError
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
            // {time} defaults to HMSus
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
            // {file} defaults to FileNameOnly
            field.spec = PatternFieldSpec::FileNameOnly;
            return {};
        }
        else if (spec == "fullpath")
        {
            field.spec = PatternFieldSpec::None;
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
    else if (field_name == "color" || field_name == "c")
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
        else if (validate_custom_color_spec(spec))
        {
            field.spec = PatternFieldSpec::ColorCustom;
            return {};
        }
        return ParsingError::InvalidFieldSpec;
    }

    return ParsingError::UnknownFieldType;
}

consteval bool validate_align_spec(std::string_view align)
{
    char positioning = align[0];
    if (positioning != '<' && positioning != '>' && positioning != '^')
    {
        return false;
    }

    std::string_view width = align.substr(1);
    if (width.size() == 0)
    {
        return false;
    }

    for (char c : width)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
    }

    return true;
}

template <size_t MaxFmtStrLen, size_t MaxFields>
struct Pattern
{
    constexpr std::string_view fmt_str_to_sv() const
    {
        return std::string_view(fmt_str, fmt_str_len);
    }

    constexpr size_t get_max_fmt_str_len()
    {
        return MaxFmtStrLen;
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
consteval std::optional<ParsingError>
expand_field(const PatternField field, std::string_view spec_name,
             std::string_view align_spec, const bool drop_color_fields,
             ParsedPatternT& result)
{
    // Returns true if the fmt str length max was exceeded, false otherwise
    auto push_to_result = [&](std::string_view str) -> bool {
        for (auto c : str)
        {
            if (result.fmt_str_len >= result.get_max_fmt_str_len())
            {
                return true;
            }
            result.fmt_str[result.fmt_str_len++] = c;
        }
        return false;
    };

    // clang-format off
#define PUSH_AND_RETURN_ON_ERROR(str)             \
    if (push_to_result(str))                      \
    {                                             \
        return ParsingError::ExceedsMaxFmtStrLen; \
    }
    // clang-format on

    if (field.type == PatternFieldType::Time)
    {
        // NOTE: Alignment is not supported for time fields since they can be
        // split into multiple fields, meaning they can't be aligned as one
        if (field.spec == PatternFieldSpec::TimeHMSus)
        {
            PUSH_AND_RETURN_ON_ERROR("{}.{:06}");
        }
        else if (field.spec == PatternFieldSpec::TimeHMSns)
        {
            PUSH_AND_RETURN_ON_ERROR("{}.{:09}");
        }
    }
    else if (field.type == PatternFieldType::Color)
    {
        if (drop_color_fields)
        {
            // Do nothing if drop_color_fields is true
        }
        else if (field.spec == PatternFieldSpec::ColorCustom)
        {
            // Parse the list of comma separated color code names, and push all
            // of them into the result
            for (auto name_it : std::views::split(spec_name, ','))
            {
                std::string_view name = { name_it.begin(), name_it.end() };
                std::string_view code = ColorCode::from_name(name);
                PUSH_AND_RETURN_ON_ERROR(code);
            }
        }
        else if (field.spec == PatternFieldSpec::ColorReset)
        {
            PUSH_AND_RETURN_ON_ERROR(ColorCode::reset);
        }
        else // ColorLevel
        {
            PUSH_AND_RETURN_ON_ERROR("{}");
        }
    }
    else
    {
        // For most fields, other than the ones handled above, simply push
        // empty brackets, optionally with an alignment spec
        if (align_spec.empty())
        {
            PUSH_AND_RETURN_ON_ERROR("{}");
        }
        else
        {
            PUSH_AND_RETURN_ON_ERROR("{:");
            PUSH_AND_RETURN_ON_ERROR(align_spec);
            PUSH_AND_RETURN_ON_ERROR("}");
        }
    }
    return {};
}

template <StaticString PatternStr, size_t MaxFields, size_t MaxFmtStrLen>
consteval auto parse_pattern(bool ignore_color_fields)
{
    Pattern<MaxFmtStrLen, MaxFields> result{};

    const std::size_t size = PatternStr.size;
    std::size_t i = 0;

    auto push_char
        = [&result](char c) { result.fmt_str[result.fmt_str_len++] = c; };

    while (i < size)
    {
        char c = PatternStr.data[i];

        if (c == '{')
        {
            // Check for escape sequence '{{'
            if (i + 1 < size && PatternStr.data[i + 1] == '{')
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
                   && PatternStr.data[bracket_close_idx] != '}')
            {
                ++bracket_close_idx;
            }
            if (bracket_close_idx == size)
            {
                result.error = ParsingError::UnterminatedBrace;
                return result;
            }

            std::string_view field_content{ &PatternStr.data[i + 1],
                                            bracket_close_idx - i - 1 };

            if (field_content.empty())
            {
                result.error = ParsingError::EmptyField;
                return result;
            }

            std::string_view field_name = "";
            std::string_view spec_name = "";
            std::string_view align = "";
            for (auto part : std::views::split(field_content, ':'))
            {

                std::string_view s = { part.begin(), part.end() };

                if (s.empty())
                {
                    result.error = ParsingError::InvalidFieldSyntax;
                    return result;
                }

                // Test if the part is an align spec
                if (validate_align_spec(s))
                {
                    align = s;
                    continue;
                }

                if (field_name.empty())
                {
                    field_name = s;
                }
                else if (spec_name.empty())
                {
                    spec_name = s;
                }
                else
                {
                    // Too many field parts
                    result.error = ParsingError::InvalidFieldSyntax;
                    return result;
                }
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

            if (field.type == PatternFieldType::Color)
            {
                // Only ColorLevel fields should be registered as actual fields
                // to be formatted at runtime, ColorReset or ColorCustom are
                // hardcoded into the fmt str at compile time
                if (!ignore_color_fields
                    && field.spec == PatternFieldSpec::ColorLevel)
                {
                    result.fields[result.field_count] = field;
                    result.field_count++;
                }
            }
            else
            {
                // Non-color fields are always registered
                result.fields[result.field_count] = field;
                result.field_count++;
            }

            // Also send ignore_color_fields to expand_field() so it knows when
            // not to place any color information in the fmt str
            err = expand_field(
                      field, spec_name, align, ignore_color_fields, result)
                      .value_or(ParsingError::None);
            if (err != ParsingError::None)
            {
                result.error = err;
                return result;
            }

            i = bracket_close_idx + 1;
            continue;
        }

        if (c == '}')
        {
            // Check for escape sequence '}}' and forward it if found
            if (i + 1 < size && PatternStr.data[i + 1] == '}')
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

[[maybe_unused]] inline std::string_view
get_level_color(const ColorConfig& colors, LogLevel level)
{
    if (level == LogLevel::Trace)
        return colors.trace.color;
    else if (level == LogLevel::Debug)
        return colors.debug.color;
    else if (level == LogLevel::Info)
        return colors.info.color;
    else if (level == LogLevel::Warn)
        return colors.warn.color;
    else if (level == LogLevel::Error)
        return colors.error.color;
    else
        return "";
}

// Note to me in the future: decltype(auto) combined with return ()
// adds a reference to the returned type, so pattern fields aren't
// actually copied
template <PatternField Field>
constexpr decltype(auto) get_field(const MessageData& data,
                                   const ColorConfig& colors)
{
    if constexpr (Field.type == PatternFieldType::Level)
        return (level_to_name(data.level));
    else if constexpr (Field.type == PatternFieldType::Color)
        return (get_level_color(colors, data.level));
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

// Need this for type erasure when switching out patterns
class PatternFormatterBase
{
  public:
    virtual ~PatternFormatterBase() = default;
    virtual std::string format(const MessageData& data,
                               const ColorConfig& colors,
                               bool use_colors) const = 0;
};

template <StaticString PatternStr, size_t MaxFields = 16,
          size_t MaxFmtStrLen = 1000> // 16 and 1000 should be enough
                                      // in basically all cases
class PatternFormatter : public PatternFormatterBase
{
  public:
    std::string format(const MessageData& data, const ColorConfig& colors,
                       bool use_colors) const override
    {
        return format_impl(
            data,
            colors,
            use_colors,
            std::make_index_sequence<_pattern_w_colors.field_count>{});
    }

  private:
    using Pattern = Pattern<MaxFmtStrLen, MaxFields>;

    // Parse the pattern twice, the second time dropping all color fields
    static constexpr Pattern _pattern_w_colors
        = parse_pattern<PatternStr, MaxFields, MaxFmtStrLen>(false);
    static constexpr Pattern _pattern_wo_colors
        = parse_pattern<PatternStr, MaxFields, MaxFmtStrLen>(true);

    // Asserts for nice error msgs
    static_assert(_pattern_w_colors.error != ParsingError::UnterminatedBrace,
                  "pattern contains unterminated '{'");
    static_assert(_pattern_w_colors.error != ParsingError::EmptyField,
                  "pattern contains empty '{}'");
    static_assert(_pattern_w_colors.error != ParsingError::UnknownFieldType,
                  "pattern contains unknown field type");
    static_assert(_pattern_w_colors.error != ParsingError::InvalidFieldSpec,
                  "pattern contains unknown format spec or invalid field/spec "
                  "combination");
    static_assert(_pattern_w_colors.error
                      != ParsingError::UnmatchedClosingBrace,
                  "pattern contains '}' with no matching "
                  "opening brace '{'");
    static_assert(_pattern_w_colors.error != ParsingError::InvalidFieldSyntax,
                  "pattern contains a field with invalid syntax");
    static_assert(_pattern_w_colors.error != ParsingError::TooManyFields,
                  "pattern contains too many fields; use "
                  "PAPPER_SET_LONG_PATTERN()");
    static_assert(_pattern_w_colors.error != ParsingError::ExceedsMaxFmtStrLen,
                  "pattern expands into a format string that exceeds the max; "
                  "use PAPPER_SET_LONG_PATTERN()");

    template <std::size_t... I>
    std::string format_impl(const MessageData& data, const ColorConfig& colors,
                            bool use_colors, std::index_sequence<I...>) const
    {
        if (use_colors)
        {
            // std::cout << _pattern_w_colors.field_count << std::endl;
            constexpr std::string_view fmt_sv
                = _pattern_w_colors.fmt_str_to_sv();

            auto args = std::tuple_cat(
                expand_and_process_arg<_pattern_w_colors.fields[I]>(
                    get_field<_pattern_w_colors.fields[I]>(data, colors))...);

            return std::apply(
                [&fmt_sv](auto... x) {
                    return fmt::vformat(fmt_sv, fmt::make_format_args(x...));
                },
                args);
        }
        else
        {
            constexpr std::string_view fmt_sv
                = _pattern_wo_colors.fmt_str_to_sv();

            auto args = std::tuple_cat(
                expand_and_process_arg<_pattern_wo_colors.fields[I]>(
                    get_field<_pattern_wo_colors.fields[I]>(data, colors))...);

            return std::apply(
                [&fmt_sv](auto... x) {
                    return fmt::vformat(fmt_sv, fmt::make_format_args(x...));
                },
                args);
        }
    }
};

// Handles switching of formatter and colors
class PatternFormatterHandler
{
  public:
    PatternFormatterHandler()
    {
        // Set default pattern and colors
        _formatter = std::make_unique<
            PatternFormatter<"{color:level}[{time:HMSus}] "
                             "{level:<5} "
                             "{color:underline}{file}:{line}{color:reset} "
                             "{color:level}{message}{color:reset}">>();
        _colors = std::make_unique<ColorConfig>();
    }

    std::string format(const MessageData& data, bool use_colors)
    {
        return _formatter->format(data, *_colors, use_colors);
    }

    void set_new_pattern_formatter(
        std::unique_ptr<const PatternFormatterBase> formatter)
    {
        _formatter = std::move(formatter);
    }

    void set_new_colors(std::unique_ptr<const ColorConfig> colors)
    {
        _colors = std::move(colors);
    }

  private:
    std::unique_ptr<const PatternFormatterBase> _formatter;
    std::unique_ptr<const ColorConfig> _colors;
};
}
