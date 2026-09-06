#ifndef _PAPPER_H_
#define _PAPPER_H_

#include "core.h"

namespace papper
{

using namespace core;

// Sets the default queue size for threads created in the future.
// Can still be overridden by allocate()
// Will NOT reallocate/touch existing thread queues.
inline void set_default_queue_size(const size_t queue_size)
{
    defaults::queue_size = queue_size;
}

inline void allocate(const size_t queue_size = defaults::queue_size)
{
    get_or_create_thread_queue(queue_size);
}

template <typename... Args>
void log(const char* fmt_str, Args&&... args)
{
    Queue& q = get_or_create_thread_queue();

    size_t total_args_size
        = (Codec<std::remove_cvref_t<Args>>::encoded_size(args) + ...);
    size_t header_plus_args_size = total_args_size + sizeof(LogEventHeader);

    LogEventHeader header;
    header.fmt_str = fmt_str;
    header.payload_size = total_args_size;
    header.decoding_fn = &decode_and_format<std::remove_cvref_t<Args>...>;

    std::byte* buffer = q.reserve_write(header_plus_args_size);
    if (buffer == nullptr)
        return; // Not enough queue space, drop the event

    // Encode header
    Codec<LogEventHeader>::encode(buffer, header);
    // Encode args
    ((Codec<std::remove_cvref_t<Args>>::encode(buffer, args)), ...);

    q.commit_write();
}

}

#endif
