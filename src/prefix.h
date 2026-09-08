#ifndef _PAPPER_PREFIX_H
#define _PAPPER_PREFIX_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace papper::prefix
{

struct LogEventMetadata
{
    uint64_t timestamp;
    std::string_view filename;
    std::string_view functionname;
    uint32_t linenumber;
    uint8_t level;
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

enum class PrefixEntry
{
    Timestamp,
    Filename,
    Functionname,
    LineNumber,
    Level,
};

constexpr bool name_to_entry_enum(std::string_view name, PrefixEntry& entry)
{
    constexpr std::pair<const char*, PrefixEntry> table[] = {
        { "timestamp", PrefixEntry::Timestamp },
        { "filename", PrefixEntry::Filename },
        { "functionname", PrefixEntry::Functionname },
        { "linenumber", PrefixEntry::LineNumber },
        { "level", PrefixEntry::Level },
    };

    for (const auto& e : table)
    {
        if (e.first == name)
        {
            entry = e.second;
            return true;
        }
    }

    return false;
}

enum class ParsingError
{
    None,
    UnterminatedBrace,
    EmptyFieldName,
    UnknownFieldName,
    TooManyFields,
    UnmatchedClosingBrace,
};

template <size_t MaxFmtStrLen, size_t MaxEntries>
struct ParsedPattern
{
    constexpr std::string_view fmt_str_to_sv() const
    {
        return std::string_view(fmt_str, fmt_str_len);
    }

    ParsingError error = ParsingError::None;

    char fmt_str[MaxFmtStrLen];
    size_t fmt_str_len = 0;

    PrefixEntry entries[MaxEntries];
    size_t entry_count = 0;
};

template <StaticString Pattern, size_t MaxEntries>
consteval auto parse_pattern()
{
    ParsedPattern<Pattern.size, MaxEntries> result{};

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

            std::string_view name(&Pattern.data[i + 1],
                                  bracket_close_idx - i - 1);

            if (name.empty())
            {
                result.error = ParsingError::EmptyFieldName;
                return result;
            }

            PrefixEntry field{};
            if (!name_to_entry_enum(name, field))
            {
                result.error = ParsingError::UnknownFieldName;
                return result;
            }

            if (result.entry_count >= MaxEntries)
            {
                result.error = ParsingError::TooManyFields;
                return result;
            }
            result.entries[result.entry_count++] = field;

            push_char('{');
            push_char('}');
            i = bracket_close_idx + 1;
            continue;
        }

        if (c == '}')
        {
            // Check for escape sequence '}}' and forward like above
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

// Note to me in the future: decltype(auto) combined with return () adds a
// reference to the returned type, so rec entries aren't actually copied
template <PrefixEntry Entry>
constexpr decltype(auto) get_field(const LogEventMetadata& rec)
{
    if constexpr (Entry == PrefixEntry::Timestamp)
        return (rec.timestamp);
    else if constexpr (Entry == PrefixEntry::Filename)
        return (rec.filename);
    else if constexpr (Entry == PrefixEntry::Functionname)
        return (rec.functionname);
    else if constexpr (Entry == PrefixEntry::LineNumber)
        return (rec.linenumber);
    else if constexpr (Entry == PrefixEntry::Level)
        return (rec.level);
    else
        static_assert(false, "entry is invalid");
}

// Base class for type erasure in PrefixFormatterHandler
class PrefixFormatterBase
{
  public:
    virtual ~PrefixFormatterBase() = default;
    virtual std::string format(const LogEventMetadata& rec) = 0;
};

template <StaticString Pattern, size_t MaxEntries = 16>
class PrefixFormatter : public PrefixFormatterBase
{
  public:
    std::string format(const LogEventMetadata& rec) override
    {
        return format_impl(
            rec, std::make_index_sequence<parsed_pattern.entry_count>{});
    }

  private:
    // Parse pattern at compile time
    static constexpr ParsedPattern<Pattern.size, MaxEntries> parsed_pattern
        = parse_pattern<Pattern, MaxEntries>();

    static_assert(parsed_pattern.error != ParsingError::UnterminatedBrace,
                  "prefix pattern contains unterminated '{'");
    static_assert(parsed_pattern.error != ParsingError::EmptyFieldName,
                  "prefix pattern contains empty '{}'");
    static_assert(parsed_pattern.error != ParsingError::UnknownFieldName,
                  "prefix pattern contains unknown field name");
    static_assert(parsed_pattern.error != ParsingError::TooManyFields,
                  "prefix pattern contains too many entries (increase limit "
                  "via PrefixFormatter<\"your pattern\", NewLimit>)");
    static_assert(
        parsed_pattern.error != ParsingError::UnmatchedClosingBrace,
        "prefix pattern contains '}' with no matching opening brace ");

    template <std::size_t... I>
    static std::string format_impl(const LogEventMetadata& rec,
                                   std::index_sequence<I...>)
    {
        constexpr auto fmt_sv = parsed_pattern.fmt_str_to_sv();
        return std::vformat(fmt_sv,
                            std::make_format_args(
                                get_field<parsed_pattern.entries[I]>(rec)...));
    }
};

class PrefixFormatterHandler
{
  public:
    PrefixFormatterHandler()
    {
        _formatter = new PrefixFormatter<"[{functionname}] ">;
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
