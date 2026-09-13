#ifndef _PAPPER_H_
#define _PAPPER_H_

#include <chrono>
#include <source_location>

#include "core.h"
#include "level.h"
#include "prefix.h"
#include "time.h"

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
    core::Backend::get_or_create_instance().queue_new_sink(
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
    core::defaults::queue_size = queue_size;
}

// Initializes the backend and initializes and allocates memory for the calling
// thread's queue
inline void allocate(const size_t queue_size = core::defaults::queue_size)
{
    core::get_or_create_thread_queue(queue_size);
}

inline void set_level(const LogLevel level)
{
    core::Backend::get_or_create_instance().set_new_minimum_log_level(level);
}

template <typename... Args>
void log(const char* fmt_str,
         const core::CallSiteStaticMetadata* static_metadata, Args&&... args)
{
    core::Queue& q = core::get_or_create_thread_queue();

    size_t total_args_size = 0;
    if constexpr (sizeof...(args) > 0)
    {
        total_args_size
            = (core::Codec<std::remove_cvref_t<Args>>::encoded_size(args)
               + ...);
    }
    size_t header_plus_args_size
        = total_args_size + sizeof(core::LogEventHeader);

    core::LogEventHeader header;
    header.fmt_str = fmt_str;
    header.decoding_fn
        = &core::decode_and_format<std::remove_cvref_t<Args>...>;
    header.static_metadata = static_metadata;
    // TODO: If the cpu doesn't have invariant tsc, we have to fall back to
    // chrono, so must figure out how to handle that
    header.timestamp = time::read_tsc();
    header.payload_size = total_args_size;

    std::byte* buffer = q.reserve_write(header_plus_args_size);
    if (buffer == nullptr)
        return; // Not enough queue space, drop the event

    // Encode header
    core::Codec<core::LogEventHeader>::encode(buffer, header);
    // Encode args
    ((core::Codec<std::remove_cvref_t<Args>>::encode(buffer, args)), ...);

    q.commit_write();
}

}

// clang-format off
#define PAPPER_LOG(log_level, fmt_str, ...)                                   \
{                                                                             \
    static constexpr papper::core::CallSiteStaticMetadata static_metadata = { \
        .level = log_level,                                                   \
        .location = std::source_location::current(),                          \
    };                                                                        \
    papper::log(fmt_str, &static_metadata __VA_OPT__(,) __VA_ARGS__);         \
}
// clang-format on

// clang-format off
#define PAPPER_SET_PREFIX(pattern)                                              \
    papper::prefix::PrefixFormatterBase* fmt                                    \
        = new papper::prefix::PrefixFormatter<pattern>;                         \
    papper::core::Backend::get_or_create_instance().queue_new_prefix_formatter( \
        fmt);
// clang-format on

// Use this if you need more fields, or if the expanded internal format
// string length reaches its max
// clang-format off
#define PAPPER_SET_LONG_PREFIX(pattern, max_fields, max_expanded_fmt_str_len)   \
    papper::prefix::PrefixFormatterBase* fmt = new papper::prefix::             \
        PrefixFormatter<pattern, max_fields, max_internal_fmt_str_len>;         \
    papper::core::Backend::get_or_create_instance()                             \
        .queue_new_prefix_formatter(fmt);
// clang-format on

#endif
