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
    detail::ControlEvent event;
    event.sink = detail::SinkEvent{
        .file = file,
    };
    detail::push_control_event(detail::ControlEventType::Sink, event);
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
    detail::ControlEvent event;
    event.flush_behaviour = {
        .mode = flush_config.flush_mode,
        .buffer_size = flush_config.buffer_size,
        .max_flush_interval_ms = flush_config.max_flush_interval_ms,
    };

    detail::push_control_event(detail::ControlEventType::FlushBehaviour,
                               event);
}

// Will enable colored output for sinks that support it
inline void enable_colors()
{
    detail::ControlEvent event;
    event.colors_enabled = detail::ColorEnabledEvent{ .enabled = true };
    detail::push_control_event(detail::ControlEventType::ColorsEnable, event);
}

// Disables colored output for all sinks
inline void disable_colors()
{
    detail::ControlEvent event;
    event.colors_enabled = detail::ColorEnabledEvent{ .enabled = false };
    detail::push_control_event(detail::ControlEventType::ColorsEnable, event);
}

inline void set_colors(const ColorConfig& colors)
{
    detail::ControlEvent event;
    event.colors = detail::ColorsEvent{ new ColorConfig{ colors } };
    detail::push_control_event(detail::ControlEventType::Colors, event);
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

// Returns true if pinning was successful, false otherwise
// Only linux supported for now, so will always return false for any other
// platform
inline bool pin_backend_thread(const int cpu_id)
{
    detail::ControlEvent event;
    event.cpu_affinity = detail::CPUAffinityEvent{ cpu_id };
    return detail::push_control_event(detail::ControlEventType::CPUAffinity,
                                      event);
}

inline void
set_backend_sleep_duration(const std::chrono::system_clock::duration duration)
{
    detail::ControlEvent event;
    event.sleep_duration = detail::BackendSleepDurationEvent{ duration };
    detail::push_control_event(detail::ControlEventType::BackendSleepDuration,
                               event);
}

} // end namespace papper

// clang-format off

// Dropping
#define PAPPER_TRACE(fmt_str, ...) \
    PAPPER_LOG_IMPL(false, papper::LogLevel::Trace, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define PAPPER_INFO(fmt_str, ...) \
    PAPPER_LOG_IMPL(false, papper::LogLevel::Info, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define PAPPER_DEBUG(fmt_str, ...) \
    PAPPER_LOG_IMPL(false, papper::LogLevel::Debug, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define PAPPER_WARN(fmt_str, ...) \
    PAPPER_LOG_IMPL(false, papper::LogLevel::Warn, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define PAPPER_ERROR(fmt_str, ...) \
    PAPPER_LOG_IMPL(false, papper::LogLevel::Error, fmt_str __VA_OPT__(, ) __VA_ARGS__)

// Blocking
#define PAPPER_TRACE_B(fmt_str, ...) \
    PAPPER_LOG_IMPL(true, papper::LogLevel::Trace, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define PAPPER_INFO_B(fmt_str, ...) \
    PAPPER_LOG_IMPL(true, papper::LogLevel::Info, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define PAPPER_DEBUG_B(fmt_str, ...) \
    PAPPER_LOG_IMPL(true, papper::LogLevel::Debug, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define PAPPER_WARN_B(fmt_str, ...) \
    PAPPER_LOG_IMPL(true, papper::LogLevel::Warn, fmt_str __VA_OPT__(, ) __VA_ARGS__)

#define PAPPER_ERROR_B(fmt_str, ...) \
    PAPPER_LOG_IMPL(true, papper::LogLevel::Error, fmt_str __VA_OPT__(, ) __VA_ARGS__)

// Shorter names
#ifdef PAPPER_DEFINE_SHORT_NAMES
// Dropping
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

// Blocking 
#define LOG_TRACE_B(fmt_str, ...) \
    PAPPER_TRACE_B(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define LOG_INFO_B(fmt_str, ...) \
    PAPPER_INFO_B(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define LOG_DEBUG_B(fmt_str, ...) \
    PAPPER_DEBUG_B(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define LOG_WARN_B(fmt_str, ...) \
    PAPPER_WARN_B(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define LOG_ERROR_B(fmt_str, ...) \
    PAPPER_ERROR_B(fmt_str  __VA_OPT__(, ) __VA_ARGS__)
#endif

// Very short names
#ifdef PAPPER_DEFINE_VERY_SHORT_NAMES
// Dropping
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

// Blocking
#define TRACE_B(fmt_str, ...) \
    PAPPER_TRACE_B(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define INFO_B(fmt_str, ...) \
    PAPPER_INFO_B(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define DEBUG_B(fmt_str, ...) \
    PAPPER_DEBUG_B(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define WARN_B(fmt_str, ...) \
    PAPPER_WARN_B(fmt_str  __VA_OPT__(, ) __VA_ARGS__)

#define ERROR_B(fmt_str, ...) \
    PAPPER_ERROR_B(fmt_str  __VA_OPT__(, ) __VA_ARGS__)
#endif

#define PAPPER_SET_PATTERN(pattern_str)                                        \
{                                                                              \
    const papper::detail::PatternFormatterBase* fmt                            \
        = new papper::detail::PatternFormatter<pattern_str>;                   \
    papper::detail::ControlEvent event;                                        \
    event.pattern = papper::detail::PatternEvent{ fmt };                       \
    papper::detail::push_control_event(                                        \
        papper::detail::ControlEventType::Pattern, event);                     \
}

// Use this version if you need more fields, or if the expanded internal format
// string length reaches its max
#define PAPPER_SET_LONG_PATTERN(pattern_str, max_fields, max_expanded_fmt_str_len) \
{                                                                                  \
    papper::detail::PatternFormatterBase* fmt = new papper::detail::               \
        PatternFormatter<pattern_str, max_fields, max_expanded_fmt_str_len>;       \
    papper::detail::ControlEvent event;                                            \
    event.pattern = papper::detail::PatternEvent{ fmt };                           \
    papper::detail::push_control_event(                                            \
        papper::detail::ControlEventType::Pattern, event);                         \
}

// clang-format on
