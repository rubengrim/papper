#ifndef _PAPPER_H_
#define _PAPPER_H_

#include <chrono>
#include <source_location>

#include "core.h"
#include "prefix.h"

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

inline void allocate(const size_t queue_size = core::defaults::queue_size)
{
    core::get_or_create_thread_queue(queue_size);
}

template <typename... Args>
struct log
{
    log(const char* fmt_str, Args&&... args,
        const std::source_location& location = std::source_location::current())
    {
        core::Queue& q = core::get_or_create_thread_queue();

        size_t total_args_size
            = (core::Codec<std::remove_cvref_t<Args>>::encoded_size(args)
               + ...);
        size_t header_plus_args_size
            = total_args_size + sizeof(core::LogEventHeader);

        uint64_t timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch())
                .count());

        prefix::LogEventMetadata metadata
            = { .timestamp = timestamp,
                .filename = location.file_name(),
                .functionname = location.function_name(),
                .linenumber = location.line(),
                .level = 0 };

        core::LogEventHeader header;
        header.fmt_str = fmt_str;
        header.payload_size = total_args_size;
        header.decoding_fn
            = &core::decode_and_format<std::remove_cvref_t<Args>...>;
        header.metadata = metadata;

        std::byte* buffer = q.reserve_write(header_plus_args_size);
        if (buffer == nullptr)
            return; // Not enough queue space, drop the event

        // Encode header
        core::Codec<core::LogEventHeader>::encode(buffer, header);
        // Encode args
        ((core::Codec<std::remove_cvref_t<Args>>::encode(buffer, args)), ...);

        q.commit_write();
    }
};

template <typename... Args>
log(const char*, Args&&...) -> log<Args...>;
}

// clang-format off
#define PAPPER_SET_PREFIX(pattern)                                              \  
{                                                                               \
    papper::prefix::PrefixFormatterBase* fmt                                    \
        = new papper::prefix::PrefixFormatter<pattern>;                         \
    papper::core::Backend::get_or_create_instance().queue_new_prefix_formatter( \
        fmt);                                                                   \
}
// clang-format on

#endif
