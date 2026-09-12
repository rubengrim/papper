#ifndef _PAPPER_PREFIX_H
#define _PAPPER_PREFIX_H

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

namespace papper::prefix
{

struct LogEventMetadata
{
    Level level;
    std::chrono::time_point<std::chrono::system_clock> timestamp;
    const char* filename;
    const char* functionname;
    uint32_t linenumber;
};

// https://blog.ganets.ky/StaticString/
template <size_t N>
struct StaticString
{
    static constexpr size_t size = N;
    std::array<char, N> data;

    constexpr StaticString(const char (&input)[N])
    {
        std::copy_n(input, N, data.begin());
    }
};

template <std::size_t N>
StaticString(const char (&)[N]) -> StaticString<N>;

enum class PrefixField
{
    Level,
    Time,
    File,
    Function,
    Line,
};

constexpr bool name_to_field_enum(std::string_view name, PrefixField& field)
{
    constexpr std::pair<std::string_view, PrefixField> table[] = {
        { "time", PrefixField::Time },         { "file", PrefixField::File },
        { "function", PrefixField::Function }, { "line", PrefixField::Line },
        { "level", PrefixField::Level },
    };

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

enum class PrefixFieldFormatSpec
{
    None,
    TimeHMS,      // 16:08:09
    TimeHMSf,     // 16:08:09.796341
    TimeHMSF,     // 16:08:09.796341126
    FileNameOnly, // file.txt
};

constexpr bool name_to_format_spec_enum(std::string_view name,
                                        PrefixFieldFormatSpec& spec)
{
    constexpr std::pair<std::string_view, PrefixFieldFormatSpec> table[] = {
        { "", PrefixFieldFormatSpec::None },
        { "HMS", PrefixFieldFormatSpec::TimeHMS },
        { "HMSf", PrefixFieldFormatSpec::TimeHMSf },
        { "HMSF", PrefixFieldFormatSpec::TimeHMSF },
        { "path", PrefixFieldFormatSpec::None },
        { "nameonly", PrefixFieldFormatSpec::FileNameOnly },
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

consteval bool validate_field_spec_combination(PrefixField field,
                                               PrefixFieldFormatSpec spec)
{
    if (field == PrefixField::Time)
    {
        if (spec == PrefixFieldFormatSpec::TimeHMS)
            return true;
        if (spec == PrefixFieldFormatSpec::TimeHMSf)
            return true;
        if (spec == PrefixFieldFormatSpec::TimeHMSF)
            return true;
    }
    else if (field == PrefixField::File)
    {
        if (spec == PrefixFieldFormatSpec::FileNameOnly)
            return true;
    }

    // If not one of the above, only None is valid spec
    if (spec == PrefixFieldFormatSpec::None)
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

    PrefixField fields[MaxFields];
    PrefixFieldFormatSpec specs[MaxFields];
    size_t field_count = 0;
};

// Given the spec enum, push the corresponding format string into result
template <typename ParsedPatternT>
consteval void expand_spec_into_result(PrefixFieldFormatSpec spec,
                                       ParsedPatternT& result)
{
    std::string_view expansion = "";

    switch (spec)
    {
    case (PrefixFieldFormatSpec::None):
        expansion = "{}";
        break;
    case (PrefixFieldFormatSpec::TimeHMS):
        expansion = "{:%H:%M:%OS}";
        break;
    case (PrefixFieldFormatSpec::TimeHMSf):
        expansion = "{:%H:%M:%S}";
        break;
    case (PrefixFieldFormatSpec::TimeHMSF):
        expansion = "{:%H:%M:%S}";
        break;
    case (PrefixFieldFormatSpec::FileNameOnly):
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

            PrefixField field;
            if (!name_to_field_enum(field_name, field))
            {
                result.error = ParsingError::UnknownFieldName;
                return result;
            }

            PrefixFieldFormatSpec spec;
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

    // Push a final space to separate prefix from message
    push_char(' ');
    return result;
}

// TODO: Remove this function and just write a custom formatter for
// papper::Level
constexpr std::string_view level_to_name(Level level)
{
    switch (level)
    {
    case Level::Trace:
        return "TRACE";
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO";
    case Level::Warn:
        return "WARN";
    case Level::Error:
        return "ERROR";
    }
}

// Note to me in the future: decltype(auto) combined with return ()
// adds a reference to the returned type, so rec entries aren't
// actually copied
template <PrefixField Field>
constexpr decltype(auto) get_field(const LogEventMetadata& rec)
{
    if constexpr (Field == PrefixField::Level)
        return (level_to_name(rec.level));
    else if constexpr (Field == PrefixField::Time)
        return (rec.timestamp);
    else if constexpr (Field == PrefixField::File)
        return (rec.filename);
    else if constexpr (Field == PrefixField::Function)
        return (rec.functionname);
    else if constexpr (Field == PrefixField::Line)
        return (rec.linenumber);
    else
        static_assert(false,
                      "field is invalid"); // Should be unreachable
}

// For some specs the arg has to be expanded into multiple args or
// converted somehow Returns a tuple with the expanded args
template <PrefixFieldFormatSpec Spec, typename T>
auto expand_and_process_format_arg(const T& arg)
{

    if constexpr (Spec == PrefixFieldFormatSpec::None)
    {
        return std::make_tuple(arg);
    }
    else if constexpr (Spec == PrefixFieldFormatSpec::TimeHMS)
    {
        static auto tz = std::chrono::current_zone();
        auto local_timepoint = tz->to_local(arg);
        return std::make_tuple(local_timepoint);
    }
    else if constexpr (Spec == PrefixFieldFormatSpec::TimeHMSf)
    {
        static auto tz = std::chrono::current_zone();
        auto local_timepoint = tz->to_local(arg);
        auto us = std::chrono::time_point_cast<std::chrono::microseconds>(
            local_timepoint);
        return std::make_tuple(us);
    }
    else if constexpr (Spec == PrefixFieldFormatSpec::TimeHMSF)
    {
        static auto tz = std::chrono::current_zone();
        auto local_timepoint = tz->to_local(arg);
        return std::make_tuple(local_timepoint);
    }
    else if constexpr (Spec == PrefixFieldFormatSpec::FileNameOnly)
    {
        std::string_view path_sv = arg;
        std::string_view filename
            = path_sv.substr(path_sv.find_last_of("/\\") + 1);
        return std::make_tuple(filename);
    }
}

// Base class used for type erasure in PrefixFormatterHandler
class PrefixFormatterBase
{
  public:
    virtual ~PrefixFormatterBase() = default;
    virtual std::string format(const LogEventMetadata& rec) = 0;
};

template <StaticString Pattern, size_t MaxFields = 16,
          size_t MaxFmtStrLen = 1000> // 16 and 1000 should be enough
                                      // in basically all cases
class PrefixFormatter : public PrefixFormatterBase
{
  public:
    std::string format(const LogEventMetadata& rec) override
    {
        return format_impl(
            rec, std::make_index_sequence<parsed_pattern.field_count>{});
    }

  private:
    static constexpr ParsedPattern<MaxFmtStrLen, MaxFields> parsed_pattern
        = parse_pattern<Pattern, MaxFields, MaxFmtStrLen>();

    // Asserts for nicer error msgs
    static_assert(parsed_pattern.error != ParsingError::UnterminatedBrace,
                  "prefix pattern contains unterminated '{'");
    static_assert(parsed_pattern.error != ParsingError::EmptyFieldName,
                  "prefix pattern contains empty '{}'");
    static_assert(parsed_pattern.error != ParsingError::UnknownFieldName,
                  "prefix pattern contains unknown field name");
    static_assert(parsed_pattern.error != ParsingError::UnknownFormatSpec,
                  "prefix pattern contains unknown format spec");
    static_assert(parsed_pattern.error != ParsingError::InvalidSpecUsage,
                  "prefix pattern contains invalid field/spec combination");
    static_assert(parsed_pattern.error != ParsingError::TooManyFields,
                  "prefix pattern contains too many fields (increase limit "
                  "via PrefixFormatter<\"your pattern\", "
                  "MaxFields=yournewlimit>)");
    static_assert(parsed_pattern.error != ParsingError::UnmatchedClosingBrace,
                  "prefix pattern contains '}' with no matching "
                  "opening brace '{'");

    template <std::size_t... I>
    std::string format_impl(const LogEventMetadata& rec,
                            std::index_sequence<I...>)
    {
        constexpr std::string_view fmt_sv = parsed_pattern.fmt_str_to_sv();

        auto args = std::tuple_cat(
            expand_and_process_format_arg<parsed_pattern.specs[I]>(
                get_field<parsed_pattern.fields[I]>(rec))...);

        return std::apply(
            [&fmt_sv](auto... x) {
                return std::vformat(fmt_sv, std::make_format_args(x...));
            },
            args);
    }
};

class PrefixFormatterHandler
{
  public:
    PrefixFormatterHandler()
    {
        // Set default pattern
        _formatter = new PrefixFormatter<
            "[{time:HMSf}] [{level}] [{file:nameonly}:{line}]">;
    }

    ~PrefixFormatterHandler()
    {
        PrefixFormatterBase* pending = _pending_new_formatter.exchange(
            nullptr, std::memory_order_acq_rel);
        if (pending != nullptr)
            delete pending;

        if (_formatter != nullptr)
            delete _formatter;
    }

    std::string format(const LogEventMetadata& rec)
    {
        return _formatter->format(rec);
    }

    void queue_new_formatter(PrefixFormatterBase* formatter)
    {
        if (formatter == nullptr)
            return;
        PrefixFormatterBase* prev = _pending_new_formatter.exchange(
            formatter, std::memory_order_acq_rel);
        if (prev != nullptr)
            delete prev;
    }

    void poll_for_pending_formatter_switch()
    {
        PrefixFormatterBase* new_formatter = _pending_new_formatter.exchange(
            nullptr, std::memory_order_acq_rel);
        if (new_formatter != nullptr)
        {
            delete _formatter;
            _formatter = new_formatter;
        }
    }

  private:
    PrefixFormatterBase* _formatter;
    std::atomic<PrefixFormatterBase*> _pending_new_formatter = nullptr;
};
}

#endif
