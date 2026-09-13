#pragma once

// IWYU pragma: always_keep

#include <source_location>

#include "detail/core.h"

namespace papper
{

enum class FlushOn
{
    LineEnd = _IOLBF,
    BufferFull = _IOFBF,
};

inline void set_sink(FILE* file, const FlushOn flush_on = FlushOn::LineEnd,
                     const size_t buffer_size = BUFSIZ)
{
    detail::Backend::get_or_create_instance().queue_new_sink(
        file, (int)flush_on, buffer_size);
}

[[nodiscard]] inline int set_sink(const char* filename,
                                  const bool truncate = false,
                                  const FlushOn flush_on = FlushOn::LineEnd,
                                  const size_t buffer_size = BUFSIZ)
{
    FILE* new_sink = fopen(filename, truncate ? "w" : "a");
    if (!new_sink)
        return errno;

    set_sink(new_sink, flush_on, buffer_size);

    return 0;
}

// Sets the default queue size for threads created in the future.
// Can still be overridden by allocate()
// Will NOT reallocate/touch existing thread queues.
inline void set_default_queue_size(const size_t queue_size)
{
    detail::defaults::queue_size = queue_size;
}

// Initializes the backend and initializes and allocates memory for the
// thread's queue
// Allows setting a thread name
inline void init_thread(std::string_view thread_name = "",
                        const size_t queue_size = detail::defaults::queue_size)
{
    detail::get_or_create_thread_queue(thread_name, queue_size);
}

inline void set_level(const LogLevel level)
{
    detail::Backend::get_or_create_instance().set_new_minimum_log_level(level);
}

} // end namespace papper

// clang-format off

#define trace(fmt_str, ...)                                                    \
    log_impl(papper::LogLevel::Trace, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define info(fmt_str, ...)                                                     \
    log_impl(papper::LogLevel::Info, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define debug(fmt_str, ...)                                                    \
    log_impl(papper::LogLevel::Debug, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define warn(fmt_str, ...)                                                     \
    log_impl(papper::LogLevel::Warn, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define error(fmt_str, ...)                                                    \
    log_impl(papper::LogLevel::Error, fmt_str __VA_OPT__(, ) __VA_ARGS__)

// log() is alias for trace()
#define log(fmt_str, ...)                                                      \
    trace(fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define papper_set_pattern(pattern)                                            \
{                                                                              \
    papper::detail::PatternFormatterBase* fmt                                  \
        = new papper::detail::PatternFormatter<pattern>;                       \
    papper::detail::Backend::get_or_create_instance()                          \
        .queue_new_pattern_formatter(fmt);                                     \
}

// Use this if you need more fields, or if the expanded internal format
// string length reaches its max
#define papper_set_long_pattern(pattern, max_fields, max_expanded_fmt_str_len) \
{                                                                              \
    papper::detail::PatternFormatterBase* fmt = new papper::detail::           \
        PatternFormatter<pattern, max_fields, max_expanded_fmt_str_len>;       \
    papper::detail::Backend::get_or_create_instance()                          \
        .queue_new_pattern_formatter(fmt);                                     \
}

// clang-format on
