#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

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
    std::chrono::time_point<std::chrono::system_clock> timestamp;
    const char* filename;
    const char* functionname;
    uint32_t linenumber;
    std::string_view message;
};

enum class PatternField
{
    Level,
    ThreadName,
    Time,
    File,
    Function,
    Line,
    Message,
};

constexpr bool name_to_field_enum(std::string_view name, PatternField& field)
{
    constexpr std::pair<std::string_view, PatternField> table[]
        = { { "time", PatternField::Time },
            { "file", PatternField::File },
            { "function", PatternField::Function },
            { "line", PatternField::Line },
            { "level", PatternField::Level },
            { "message", PatternField::Message },
            { "m", PatternField::Message },
            { "thread", PatternField::ThreadName } };

    for (const auto& e : table)
    {
        if (e.first == name)
        {
            field = e.second;
            return true;
        }
    }

    return false;
}

enum class PatternFieldFormatSpec
{
    None,
    TimeHMS,      // 16:08:09
    TimeHMSf,     // 16:08:09.796341
    TimeHMSF,     // 16:08:09.796341126
    FileNameOnly, // file.txt instead of /home/user/file.txt
};

constexpr bool name_to_format_spec_enum(std::string_view name,
                                        PatternFieldFormatSpec& spec)
{
    constexpr std::pair<std::string_view, PatternFieldFormatSpec> table[] = {
        { "", PatternFieldFormatSpec::None },
        { "HMS", PatternFieldFormatSpec::TimeHMS },
        { "HMSf", PatternFieldFormatSpec::TimeHMSf },
        { "HMSF", PatternFieldFormatSpec::TimeHMSF },
        { "path", PatternFieldFormatSpec::None },
        { "nameonly", PatternFieldFormatSpec::FileNameOnly },
    };

    for (const auto& e : table)
    {
        if (e.first == name)
        {
            spec = e.second;
            return true;
        }
    }

    return false;
}

consteval bool validate_field_spec_combination(PatternField field,
                                               PatternFieldFormatSpec spec)
{
    if (field == PatternField::Time)
    {
        if (spec == PatternFieldFormatSpec::TimeHMS)
            return true;
        if (spec == PatternFieldFormatSpec::TimeHMSf)
            return true;
        if (spec == PatternFieldFormatSpec::TimeHMSF)
            return true;
    }
    else if (field == PatternField::File)
    {
        if (spec == PatternFieldFormatSpec::FileNameOnly)
            return true;
    }

    // If not one of the above, only None is valid spec
    if (spec == PatternFieldFormatSpec::None)
        return true;

    return false;
}

enum class ParsingError
{
    None,
    UnterminatedBrace,
    EmptyFieldName,
    UnknownFieldName,
    UnknownFormatSpec,
    InvalidSpecUsage,
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
    PatternFieldFormatSpec specs[MaxFields];
    size_t field_count = 0;
};

// Given the spec enum, push the corresponding format string into result
template <typename ParsedPatternT>
consteval void expand_spec_into_result(PatternFieldFormatSpec spec,
                                       ParsedPatternT& result)
{
    std::string_view expansion = "";

    switch (spec)
    {
    case (PatternFieldFormatSpec::None):
        expansion = "{}";
        break;
    case (PatternFieldFormatSpec::TimeHMS):
        expansion = "{:%H:%M:%OS}";
        break;
    case (PatternFieldFormatSpec::TimeHMSf):
        expansion = "{:%H:%M:%S}";
        break;
    case (PatternFieldFormatSpec::TimeHMSF):
        expansion = "{:%H:%M:%S}";
        break;
    case (PatternFieldFormatSpec::FileNameOnly):
        expansion = "{}";
        break;
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
                // Forward escape sequence to std::vformat
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
            if (!name_to_field_enum(field_name, field))
            {
                result.error = ParsingError::UnknownFieldName;
                return result;
            }

            PatternFieldFormatSpec spec;
            if (!name_to_format_spec_enum(spec_name, spec))
            {
                result.error = ParsingError::UnknownFormatSpec;
                return result;
            }

            if (!validate_field_spec_combination(field, spec))
            {
                result.error = ParsingError::InvalidSpecUsage;
                return result;
            }

            if (result.field_count >= MaxFields)
            {
                result.error = ParsingError::TooManyFields;
                return result;
            }

            result.specs[result.field_count] = spec;
            result.fields[result.field_count] = field;
            result.field_count++;

            expand_spec_into_result(spec, result);

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
// papper::Level
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

// Note to me in the future: decltype(auto) combined with return ()
// adds a reference to the returned type, so pattern fields aren't
// actually copied
template <PatternField Field>
constexpr decltype(auto) get_field(const PatternData& data)
{
    if constexpr (Field == PatternField::Level)
        return (level_to_name(data.level));
    else if constexpr (Field == PatternField::Time)
        return (data.timestamp);
    else if constexpr (Field == PatternField::File)
        return (data.filename);
    else if constexpr (Field == PatternField::Function)
        return (data.functionname);
    else if constexpr (Field == PatternField::Line)
        return (data.linenumber);
    else if constexpr (Field == PatternField::Message)
        return (data.message);
    else if constexpr (Field == PatternField::ThreadName)
        return (data.thread_name);
    else
        static_assert(false,
                      "field is invalid"); // Should be unreachable
}

// For some specs the arg has to be expanded into multiple args or
// converted somehow Returns a tuple with the expanded args
template <PatternFieldFormatSpec Spec, typename T>
auto expand_and_process_format_arg(const T& arg)
{

    if constexpr (Spec == PatternFieldFormatSpec::None)
    {
        return std::make_tuple(arg);
    }
    else if constexpr (Spec == PatternFieldFormatSpec::TimeHMS)
    {
        static auto tz = std::chrono::current_zone();
        auto local_timepoint = tz->to_local(arg);
        return std::make_tuple(local_timepoint);
    }
    else if constexpr (Spec == PatternFieldFormatSpec::TimeHMSf)
    {
        static auto tz = std::chrono::current_zone();
        auto local_timepoint = tz->to_local(arg);
        auto us = std::chrono::time_point_cast<std::chrono::microseconds>(
            local_timepoint);
        return std::make_tuple(us);
    }
    else if constexpr (Spec == PatternFieldFormatSpec::TimeHMSF)
    {
        static auto tz = std::chrono::current_zone();
        auto local_timepoint = tz->to_local(arg);
        return std::make_tuple(local_timepoint);
    }
    else if constexpr (Spec == PatternFieldFormatSpec::FileNameOnly)
    {
        std::string_view path_sv = arg;
        std::string_view filename
            = path_sv.substr(path_sv.find_last_of("/\\") + 1);
        return std::make_tuple(filename);
    }
}

// Base class used for type erasure in PatternFormatterHandler
class PatternFormatterBase
{
  public:
    virtual ~PatternFormatterBase() = default;
    virtual std::string format(const PatternData& data) = 0;
};

template <StaticString Pattern, size_t MaxFields = 16,
          size_t MaxFmtStrLen = 1000> // 16 and 1000 should be enough
                                      // in basically all cases
class PatternFormatter : public PatternFormatterBase
{
  public:
    std::string format(const PatternData& data) override
    {
        return format_impl(
            data, std::make_index_sequence<parsed_pattern.field_count>{});
    }

  private:
    static constexpr ParsedPattern<MaxFmtStrLen, MaxFields> parsed_pattern
        = parse_pattern<Pattern, MaxFields, MaxFmtStrLen>();

    // Asserts for nicer error msgs
    static_assert(parsed_pattern.error != ParsingError::UnterminatedBrace,
                  "pattern contains unterminated '{'");
    static_assert(parsed_pattern.error != ParsingError::EmptyFieldName,
                  "pattern contains empty '{}'");
    static_assert(parsed_pattern.error != ParsingError::UnknownFieldName,
                  "pattern contains unknown field name");
    static_assert(parsed_pattern.error != ParsingError::UnknownFormatSpec,
                  "pattern contains unknown format spec");
    static_assert(parsed_pattern.error != ParsingError::InvalidSpecUsage,
                  "pattern contains invalid field/spec combination");
    static_assert(parsed_pattern.error != ParsingError::TooManyFields,
                  "pattern contains too many fields (use "
                  "papper_set_long_pattern() instead)");
    static_assert(parsed_pattern.error != ParsingError::UnmatchedClosingBrace,
                  "pattern contains '}' with no matching "
                  "opening brace '{'");

    template <std::size_t... I>
    std::string format_impl(const PatternData& data, std::index_sequence<I...>)
    {
        constexpr std::string_view fmt_sv = parsed_pattern.fmt_str_to_sv();

        auto args = std::tuple_cat(
            expand_and_process_format_arg<parsed_pattern.specs[I]>(
                get_field<parsed_pattern.fields[I]>(data))...);

        return std::apply(
            [&fmt_sv](auto... x) {
                return std::vformat(fmt_sv, std::make_format_args(x...));
            },
            args);
    }
};

class PatternFormatterHandler
{
  public:
    PatternFormatterHandler()
    {
        // Set default pattern
        _formatter
            = new PatternFormatter<"[{time:HMSf}] ({level}) "
                                   "({file:nameonly}:{line}) {message}">;
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

    std::string format(const PatternData& data)
    {
        return _formatter->format(data);
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
};
}
