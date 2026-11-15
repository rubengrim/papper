#pragma once

// IWYU pragma: always_keep

#include "detail/core.h"
#include "papper/detail/control_events.h"
#include "papper/detail/globals.h"
#include "papper/detail/sink.h"
#include <string_view>

namespace papper
{

inline void set_sink(FILE* file)
{
    detail::ControlEvent req;
    req.new_sink = detail::NewSinkEvent{
        .file = file,
    };
    detail::push_control_event(detail::ControlEventType::NewSink, req);
}

// Returns 0 on success, errno if file opening failed
[[nodiscard]] inline int set_sink(const char* filename,
                                  const bool truncate = false)
{
    FILE* new_sink = fopen(filename, truncate ? "w" : "a");
    if (!new_sink)
    {
        return errno;
    }

    set_sink(new_sink);

    return 0;
}

inline void set_flush_behaviour(const FlushConfig& flush_config)
{
    detail::ControlEvent req;
    req.new_flush_behaviour = {
        .mode = flush_config.flush_mode,
        .buffer_size = flush_config.buffer_size,
        .max_flush_interval_ms = flush_config.max_flush_interval_ms,
    };

    detail::push_control_event(detail::ControlEventType::NewFlushConfig, req);
}

// Will enable colored output for sinks that support it
inline void enable_colors()
{
    detail::ControlEvent event;
    event.colors_enabled = detail::ColorEnabledEvent{ .enabled = true };
    detail::push_control_event(detail::ControlEventType::ColorEnable, event);
}

// Disables colored output for all sinks
inline void disable_colors()
{
    detail::ControlEvent event;
    event.colors_enabled = detail::ColorEnabledEvent{ .enabled = false };
    detail::push_control_event(detail::ControlEventType::ColorEnable, event);
}

inline void set_colors(const ColorConfig& colors)
{
    detail::ControlEvent event;
    event.new_colors = detail::NewColorsEvent{ new ColorConfig{ colors } };
    detail::push_control_event(detail::ControlEventType::NewColors, event);
}

// Sets the default queue size for threads created in the future.
// Can still be overridden by allocate()
// Will NOT reallocate/touch existing thread queues.
inline void set_default_queue_size(const size_t queue_size)
{
    detail::globals::default_queue_size = queue_size;
}

// Initializes the backend and initializes and allocates memory for the
// thread's queue
inline void init_thread(const size_t queue_size
                        = detail::globals::default_queue_size)
{
    detail::get_or_init_thread_context(queue_size);
}

inline void set_thread_name(std::string_view name)
{
    detail::ThreadContext* context = detail::get_or_init_thread_context();
    if (context != nullptr)
    {
        context->set_name(name);
    }
}

inline void set_level(const LogLevel level)
{
    detail::Backend::get_or_create_instance().set_new_minimum_log_level(level);
}

} // end namespace papper

// clang-format off

#define PAPPER_TRACE(fmt_str, ...) \
    PAPPER_LOG_IMPL(papper::LogLevel::Trace, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define PAPPER_INFO(fmt_str, ...) \
    PAPPER_LOG_IMPL(papper::LogLevel::Info, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define PAPPER_DEBUG(fmt_str, ...) \
    PAPPER_LOG_IMPL(papper::LogLevel::Debug, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define PAPPER_WARN(fmt_str, ...) \
    PAPPER_LOG_IMPL(papper::LogLevel::Warn, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define PAPPER_ERROR(fmt_str, ...) \
    PAPPER_LOG_IMPL(papper::LogLevel::Error, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#ifdef PAPPER_DEFINE_SHORT_NAMES
#define LOG_TRACE(fmt_str, ...) \
    PAPPER_TRACE(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define LOG_INFO(fmt_str, ...) \
    PAPPER_INFO(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define LOG_DEBUG(fmt_str, ...) \
    PAPPER_DEBUG(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define LOG_WARN(fmt_str, ...) \
    PAPPER_WARN(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define LOG_ERROR(fmt_str, ...) \
    PAPPER_ERROR(fmt_str  __VA_OPT__(, ) __VA_ARGS__)
#endif

#ifdef PAPPER_DEFINE_VERY_SHORT_NAMES
#define TRACE(fmt_str, ...) \
    PAPPER_TRACE(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define INFO(fmt_str, ...) \
    PAPPER_INFO(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define DEBUG(fmt_str, ...) \
    PAPPER_DEBUG(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define WARN(fmt_str, ...) \
    PAPPER_WARN(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define ERROR(fmt_str, ...) \
    PAPPER_ERROR(fmt_str  __VA_OPT__(, ) __VA_ARGS__)
#endif

#define PAPPER_SET_PATTERN(pattern)                                            \
{                                                                              \
    const papper::detail::PatternFormatterBase* fmt                            \
        = new papper::detail::PatternFormatter<pattern>;                       \
    papper::detail::ControlEvent event;                                        \
    event.new_pattern = papper::detail::NewPatternEvent{ fmt };                \
    papper::detail::push_control_event(                                        \
        papper::detail::ControlEventType::NewPattern, event);                  \
}

// Use this version if you need more fields, or if the expanded internal format
// string length reaches its max
#define PAPPER_SET_LONG_PATTERN(pattern, max_fields, max_expanded_fmt_str_len) \
{                                                                              \
    papper::detail::PatternFormatterBase* fmt = new papper::detail::           \
        PatternFormatter<pattern, max_fields, max_expanded_fmt_str_len>;       \
    papper::detail::ControlEvent event;                                        \
    event.new_pattern = papper::detail::NewPatternEvent{ fmt };                \
    papper::detail::push_control_event(                                        \
        papper::detail::ControlEventType::NewPattern, event);                  \
}

// clang-format on
