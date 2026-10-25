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

struct PatternData
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
    TimeRaw,      // Raw nanosecond timestamp
    TimeHMS,      // 16:08:09
    TimeHMSf,     // 16:08:09.796341
    TimeHMSF,     // 16:08:09.796341126
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

// Given a type name and spec name, get the corresponding PatternField variant.
// Returns 0 on success, 1 if the field name is invalid, and 2 if the spec is
// invalid or can't be used for the specific field.
consteval int get_and_validate_field(const std::string_view field_name,
                                     const std::string_view spec,
                                     PatternField& field)
{
    if (field_name == "level")
    {
        field.type = PatternFieldType::Level;
        if (spec == "")
        {
            field.spec = PatternFieldSpec::None;
            return 0;
        }
        return 2;
    }
    else if (field_name == "thread")
    {
        field.type = PatternFieldType::ThreadName;
        if (spec == "")
        {
            field.spec = PatternFieldSpec::None;
            return 0;
        }
        return 2;
    }
    else if (field_name == "time")
    {
        field.type = PatternFieldType::Time;
        if (spec == "")
        {
            field.spec = PatternFieldSpec::TimeRaw;
            return 0;
        }
        else if (spec == "HMS")
        {
            field.spec = PatternFieldSpec::TimeHMS;
            return 0;
        }
        else if (spec == "HMSf")
        {
            field.spec = PatternFieldSpec::TimeHMSf;
            return 0;
        }
        else if (spec == "HMSF")
        {
            field.spec = PatternFieldSpec::TimeHMSF;
            return 0;
        }
        return 2;
    }
    else if (field_name == "file")
    {
        field.type = PatternFieldType::File;
        if (spec == "")
        {
            field.spec = PatternFieldSpec::FileFullPath;
            return 0;
        }
        else if (spec == "nameonly")
        {
            field.spec = PatternFieldSpec::FileNameOnly;
            return 0;
        }
        return 2;
    }
    else if (field_name == "function")
    {
        field.type = PatternFieldType::Function;
        if (spec == "")
        {
            field.spec = PatternFieldSpec::None;
            return 0;
        }
        return 2;
    }
    else if (field_name == "line")
    {
        field.type = PatternFieldType::Line;
        if (spec == "")
        {
            field.spec = PatternFieldSpec::None;
            return 0;
        }
        return 2;
    }
    else if (field_name == "message")
    {
        field.type = PatternFieldType::Message;
        if (spec == "")
        {
            field.spec = PatternFieldSpec::None;
            return 0;
        }
        return 2;
    }
    else if (field_name == "color")
    {
        field.type = PatternFieldType::Color;
        if (spec == "level")
        {
            field.spec = PatternFieldSpec::ColorLevel;
            return 0;
        }
        else if (spec == "reset")
        {
            field.spec = PatternFieldSpec::ColorReset;
            return 0;
        }
        else if (spec == "blue")
        {
            field.spec = PatternFieldSpec::ColorBlue;
            return 0;
        }
        return 2;
    }

    return 1;
}

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
// For instance, {time:HMSf} in the pattern needs to be
// expanded into the actual format string: {}.{:06}
template <typename ParsedPatternT>
consteval void expand_field(PatternField field, ParsedPatternT& result)
{
    // All fields and specs other than the ones handled below can be expanded
    // into basic brackets
    std::string_view expansion = "{}";

    if (field.spec == PatternFieldSpec::TimeHMSf)
    {
        expansion = "{}.{:06}";
    }
    else if (field.spec == PatternFieldSpec::TimeHMSF)
    {
        expansion = "{}.{:09}";
    }

    // Push to fmt str
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
            int res = get_and_validate_field(field_name, spec_name, field);
            if (res == 1)
            {
                result.error = ParsingError::UnknownFieldType;
                return result;
            }
            else if (res == 2)
            {
                result.error = ParsingError::InvalidFieldSpec;
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

// TODO: Remove this function and just write a custom formatter for
// papper::LogLevel
constexpr std::string_view level_to_name(LogLevel level)
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

template <PatternFieldSpec Spec>
std::string_view get_color(const LogLevel level,
                           const ColorConfiguration* colors)
{
    // nullptr means the sink does not want colored output, so expand field to
    // nothing
    if (colors == nullptr)
    {
        return "";
    }

    if constexpr (Spec == PatternFieldSpec::ColorLevel)
        return colors->level_color(level);
    else if constexpr (Spec == PatternFieldSpec::ColorReset)
        return Color::reset;
    else if constexpr (Spec == PatternFieldSpec::ColorBlue)
        return Color::blue;
    else
        static_assert(sizeof(int) == 0,
                      "spec is not a color"); // Should be unreachable
}

// Note to me in the future: decltype(auto) combined with return ()
// adds a reference to the returned type, so pattern fields aren't
// copied
template <PatternField Field>
constexpr decltype(auto) get_field(const PatternData& data,
                                   const ColorConfiguration* colors)
{
    constexpr PatternFieldType FieldType = Field.type;

    if constexpr (FieldType == PatternFieldType::Color)
        return get_color<Field.spec>(data.level, colors);
    else if constexpr (FieldType == PatternFieldType::Level)
        return (level_to_name(data.level));
    else if constexpr (FieldType == PatternFieldType::Time)
        return (data.timestamp);
    else if constexpr (FieldType == PatternFieldType::File)
        return (data.filename);
    else if constexpr (FieldType == PatternFieldType::Function)
        return (data.functionname);
    else if constexpr (FieldType == PatternFieldType::Line)
        return (data.linenumber);
    else if constexpr (FieldType == PatternFieldType::Message)
        return (data.message);
    else if constexpr (FieldType == PatternFieldType::ThreadName)
        return (data.thread_name);
    else
        static_assert(sizeof(int) == 0,
                      "field is invalid"); // Should be unreachable
}

template <PatternFieldSpec Spec>
inline auto
process_time_arg(const std::chrono::sys_time<std::chrono::nanoseconds>& arg)
{
    static constinit std::string HMS_part = "";
    static constinit auto last_update
        = std::chrono::sys_time<std::chrono::seconds>(
            std::chrono::seconds(-1));

    // Floor to whole seconds, fmt prints sub-second digits for %S/%OS
    // when the time_point has a finer precision than seconds
    const auto arg_s = std::chrono::floor<std::chrono::seconds>(arg);
    if (arg_s != last_update)
    {
        HMS_part.clear();
        fmt::format_to(std::back_inserter(HMS_part), "{:%H:%M:%S}", arg_s);
        last_update = arg_s;
    }

    if constexpr (Spec == PatternFieldSpec::TimeHMS)
    {
        return std::make_tuple(HMS_part);
    }
    else if constexpr (Spec == PatternFieldSpec::TimeHMSf)
    {
        uint64_t us_part
            = std::chrono::floor<std::chrono::microseconds>(
                  (arg.time_since_epoch() % std::chrono::seconds(1)))
                  .count();
        return std::make_tuple(HMS_part, us_part);
    }
    else if constexpr (Spec == PatternFieldSpec::TimeHMSF)
    {
        uint64_t ns_part
            = (arg.time_since_epoch() % std::chrono::seconds(1)).count();
        return std::make_tuple(HMS_part, ns_part);
    }
}

// For some specs the arg has to be expanded into multiple args or
// processed somehow.
// Returns a tuple with the expanded args
template <PatternFieldSpec Spec, typename T>
auto expand_and_process_format_arg(const T& arg)
{
    if constexpr (Spec == PatternFieldSpec::TimeHMS)
    {
        return process_time_arg<Spec>(arg);
    }
    else if constexpr (Spec == PatternFieldSpec::TimeHMSf)
    {
        return process_time_arg<Spec>(arg);
    }
    else if constexpr (Spec == PatternFieldSpec::TimeHMSF)
    {
        return process_time_arg<Spec>(arg);
    }
    else if constexpr (Spec == PatternFieldSpec::FileNameOnly)
    {
        std::string_view path_sv = arg;
        std::string_view filename
            = path_sv.substr(path_sv.find_last_of("/\\") + 1);
        return std::make_tuple(filename);
    }
    else
    {
        return std::make_tuple(arg);
    }
}

class PatternFormatterBase
{
  public:
    virtual ~PatternFormatterBase() = default;
    virtual std::string format(const PatternData& data,
                               const ColorConfiguration* colors) = 0;
};

template <StaticString Pattern, size_t MaxFields = 16,
          size_t MaxFmtStrLen = 1000>
class PatternFormatter : public PatternFormatterBase
{
  public:
    std::string format(const PatternData& data,
                       const ColorConfiguration* colors) override
    {
        return format_impl(
            data, colors, std::make_index_sequence<_fmt_str.field_count>{});
    }

  private:
    static constexpr ParsedPattern<MaxFmtStrLen, MaxFields> _fmt_str
        = parse_pattern<Pattern, MaxFields, MaxFmtStrLen>();

    // Asserts for nicer error msgs
    static_assert(_fmt_str.error != ParsingError::UnterminatedBrace,
                  "pattern contains unterminated '{'");
    static_assert(_fmt_str.error != ParsingError::EmptyFieldName,
                  "pattern contains empty '{}'");
    static_assert(_fmt_str.error != ParsingError::UnknownFieldType,
                  "pattern contains unknown field name");
    static_assert(_fmt_str.error != ParsingError::InvalidFieldSpec,
                  "pattern contains unknown or invalid format spec");
    static_assert(_fmt_str.error != ParsingError::TooManyFields,
                  "pattern contains too many fields (use "
                  "papper_set_long_pattern() instead)");
    static_assert(_fmt_str.error != ParsingError::UnmatchedClosingBrace,
                  "pattern contains '}' with no matching "
                  "opening brace '{'");

    template <size_t... I>
    std::string format_impl(const PatternData& data,
                            const ColorConfiguration* colors,
                            std::index_sequence<I...>)
    {
        constexpr std::string_view fmt_sv = _fmt_str.fmt_str_to_sv();

        auto args = std::tuple_cat(
            expand_and_process_format_arg<_fmt_str.fields[I].spec>(
                get_field<_fmt_str.fields[I]>(data, colors))...);

        return std::apply(
            [&fmt_sv](auto... x) {
                return fmt::vformat(fmt_sv, fmt::make_format_args(x...));
            },
            args);
    }
};

// Wrapper to handle runtime switching of formatter
class PatternFormatterHandler
{
  public:
    PatternFormatterHandler()
    {
        // Set default pattern
        _formatter = new PatternFormatter<
            "{color:level}[{time:HMSf}] ({level}) "
            "({file:nameonly}:{line}) {message}{color:reset}">;
    }

    ~PatternFormatterHandler()
    {
        PatternFormatterBase* pending = _pending_new_formatter.exchange(
            nullptr, std::memory_order_acq_rel);
        if (pending != nullptr)
            delete pending;

        if (_formatter != nullptr)
            delete _formatter;

        ColorConfiguration* pending_colors
            = _pending_new_colors.exchange(nullptr, std::memory_order_acq_rel);
        if (pending_colors != nullptr)
            delete pending_colors;
    }

    std::string format(const PatternData& data, const bool colored_output)
    {
        return _formatter->format(data, colored_output ? &_colors : nullptr);
    }

    // Can be called by any thread
    void queue_new_colors(const ColorConfiguration& colors)
    {
        ColorConfiguration* prev = _pending_new_colors.exchange(
            new ColorConfiguration{ colors }, std::memory_order_acq_rel);
        if (prev != nullptr)
            delete prev;
    }

    // May ONLY be called by the backend thread
    void poll_for_pending_colors_switch()
    {
        ColorConfiguration* new_colors
            = _pending_new_colors.exchange(nullptr, std::memory_order_acq_rel);
        if (new_colors != nullptr)
        {
            _colors = std::move(*new_colors);
            delete new_colors;
        }
    }

    void queue_new_formatter(PatternFormatterBase* formatter)
    {
        if (formatter == nullptr)
            return;
        PatternFormatterBase* prev = _pending_new_formatter.exchange(
            formatter, std::memory_order_acq_rel);
        if (prev != nullptr)
            delete prev;
    }

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

  private:
    PatternFormatterBase* _formatter;
    std::atomic<PatternFormatterBase*> _pending_new_formatter = nullptr;

    ColorConfiguration _colors;
    std::atomic<ColorConfiguration*> _pending_new_colors = nullptr;
};
}
