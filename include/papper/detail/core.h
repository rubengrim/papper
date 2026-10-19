#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <fmt/format.h>

#include "codec.h"
#include "level.h"
#include "pattern.h"
#include "queue.h"
#include "sink.h"
#include "time.h"

namespace papper::detail
{

namespace defaults
{

inline size_t queue_size = 1048576; // 1Mb

}

template <typename... Args>
std::string decode_and_format(const char* fmt_str, const std::byte*& args_data)
{
    using ArgsTuple = std::tuple<Args...>;
    ArgsTuple args;

    // Decode arguments into tuple
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (Codec<std::tuple_element_t<Is, ArgsTuple>>::decode(
             args_data, std::get<Is>(args)),
         ...);
    }(std::index_sequence_for<Args...>{});

    // Format
    return std::apply(
        [&](auto&&... v) {
            return fmt::vformat(fmt_str, fmt::make_format_args(v...));
        },
        args);
}

template <typename... Args>
struct DecodingFunction
{
    static constexpr auto value = &decode_and_format<Args...>;
};

// Wrapper so that decode_and_format can be specialized from a macro
template <typename... Args>
DecodingFunction<std::remove_cvref_t<Args>...>
get_decoding_function(Args&&...);

struct CallSiteStaticData
{
    LogLevel level;
    const char* fmt_str = "";
    std::string (*decoding_fn)(const char*, const std::byte*&);
    std::source_location location;
};

struct LogEventHeader
{
    const CallSiteStaticData* static_data;
    const char* thread_name;
    uint64_t timestamp;
    size_t payload_size;
};

// // push_log_event() encodes the header field by field, so the struct must
// // have no padding for decoding it as a whole to work
// static_assert(sizeof(LogEventHeader)
//               == sizeof(CallSiteStaticData*) + sizeof(const char*)
//                      + sizeof(uint64_t) + sizeof(size_t));

class ThreadContext
{
  public:
    ThreadContext(std::string_view thread_name, const size_t queue_size)
        : _q{ queue_size }, _is_alive{ true }
    {
        if (thread_name == "")
        {
            static std::atomic<int> unnamed_thread_counter{ 0 };
            _name = "thread_" + std::to_string(++unnamed_thread_counter);
        }
        else
        {
            _name = thread_name;
        }
    }

    Queue& get_queue()
    {
        return _q;
    }

    const char* get_name()
    {
        return _name.c_str();
    }

    bool thread_is_alive()
    {
        return _is_alive.load(std::memory_order_acquire);
    }

    void kill()
    {
        _is_alive.store(false, std::memory_order_release);
    }

  private:
    std::string _name;
    Queue _q;
    std::atomic<bool> _is_alive;
};

struct ThreadContextNode
{
    ThreadContext* context;
    ThreadContextNode* next{ nullptr };
};

class Backend
{
  public:
    static Backend& get_or_create_instance()
    {
        static Backend instance{};
        return instance;
    }

    Backend(const Backend&) = delete;
    Backend(Backend&&) = delete;
    Backend& operator=(const Backend&) = delete;
    Backend& operator=(Backend&&) = delete;

    ~Backend()
    {
        _running.store(false, std::memory_order_release);

        if (_backend_thread.joinable())
        {
            _backend_thread.join();
        }

        // Only free after the backend thread, which uses it, has stopped
        delete[] _queue_copy;
    }

  public:
    void register_thread(ThreadContext* context)
    {
        ThreadContextNode* node = new ThreadContextNode;
        node->context = context;
        node->next = _head.load(std::memory_order_relaxed);
        while (!_head.compare_exchange_weak(node->next,
                                            node,
                                            std::memory_order_release,
                                            std::memory_order_relaxed))
        {
        }
    }

    void queue_new_sink(FILE* file, const int buffering_mode,
                        const size_t buffer_size)
    {
        _sink.queue_new_sink(file, buffering_mode, buffer_size);
    }

    void queue_new_pattern_formatter(PatternFormatterBase* formatter)
    {
        _pattern_formatter.queue_new_formatter(formatter);
    }

    void set_new_minimum_log_level(const LogLevel new_level)
    {
        min_log_level.store(new_level, std::memory_order_release);
    }

  private:
    // May ONLY be called by the backend thread
    void remove_and_delete_node(ThreadContextNode* node)
    {
        if (node == nullptr)
            return;

        // If node is head, set head to node->next and delete node
        ThreadContextNode* expected = node;
        if (_head.compare_exchange_strong(expected,
                                          node->next,
                                          std::memory_order_release,
                                          std::memory_order_relaxed))
        {
            delete node->context;
            delete node;

            return;
        }

        ThreadContextNode* current = expected;
        ThreadContextNode* previous = nullptr;
        while (current != nullptr && current != node)
        {
            previous = current;
            current = current->next;
        }

        if (current == nullptr)
            return; // node was not found in list

        previous->next = current->next;

        delete node->context;
        delete node;
    }

    void process_events(const std::byte* buffer, const size_t size)
    {
        size_t ptr = 0;
        while (ptr < size)
        {
            LogEventHeader header;
            Codec<LogEventHeader>::decode(buffer, header);
            ptr += sizeof(LogEventHeader) + header.payload_size;

            std::string message = header.static_data->decoding_fn(
                header.static_data->fmt_str, buffer);

            if (header.static_data->level
                < min_log_level.load(std::memory_order_acquire))
            {
                continue; // Drop the event if too low level
            }

            PatternData pattern_data = {
                .level = header.static_data->level,
                .thread_name = header.thread_name,
                .timestamp
                = _clock.timestamp_to_local_system_time(header.timestamp),
                .filename = header.static_data->location.file_name(),
                .functionname = header.static_data->location.function_name(),
                .linenumber = header.static_data->location.line(),
                .message = message,
            };
            std::string formatted_message
                = _pattern_formatter.format(pattern_data);

            _sink.write_str_and_endl(formatted_message);
        }
    }

    // Checks all threads for events and returns true if there were any
    bool poll_threads()
    {
        bool b_were_events_to_process = false;

        ThreadContextNode* node = _head.load(std::memory_order_acquire);
        while (node != nullptr)
        {
            ThreadContextNode* next = node->next;
            Queue& q = node->context->get_queue();

            // const size_t q_capacity = q.get_capacity();
            // if (q_capacity > _queue_copy_size)
            // {
            //     delete[] _queue_copy;
            //     _queue_copy = new std::byte[q_capacity];
            //     _queue_copy_size = q_capacity;
            // }

            size_t n_bytes_read = 0;
            const std::byte* data = q.read_all_available(n_bytes_read);
            if (data != nullptr)
            {
                // Make local copy of data, release the queue, and then process
                // from the copy
                // std::memcpy(_queue_copy, data, n_bytes_read);
                process_events(data, n_bytes_read);
                q.commit_read();
                b_were_events_to_process = true;
            }
            else if (!node->context->thread_is_alive())
            {
                // Delete and remove the node+context from the
                // list if the thread has been killed and there
                // are no events left in the queue to process
                remove_and_delete_node(node);
            }

            node = next;
        }

        return b_were_events_to_process;
    }

    Backend()
    {
        if (!_clock.init())
        {
            throw std::runtime_error("CPU does not have an invariant TSC. "
                                     "A fallback is no implemented, so papper "
                                     "can't run on this machine yet.");
        }

        _running.store(true, std::memory_order_relaxed);
        _backend_thread = std::thread([this]() {
            uint32_t backoff_us = 1;
            // TODO: Let user to change this
            const uint32_t max_backoff_us = 100;

            const double resync_utc_offset_interval_ns = 1e6 * 1000.0;
            const double resync_to_system_clock_interval_ns = 1e6 * 100.0;

            double last_utc_offset_resync = 0;
            double last_system_clock_resync = 0;

            auto try_resync_clock = [&]() {
                double now = _clock.read_timestamp_ns();
                if (now - last_utc_offset_resync
                    > resync_utc_offset_interval_ns)
                {
                    _clock.recalculate_utc_offset();
                    last_utc_offset_resync = now;
                }

                now = _clock.read_timestamp_ns();
                if (now - last_system_clock_resync
                    > resync_to_system_clock_interval_ns)
                {
                    _clock.sync_to_system_clock();
                    last_system_clock_resync = now;
                }
            };

            while (_running.load(std::memory_order_acquire))
            {
                _sink.poll_for_pending_sink_switch();
                _pattern_formatter.poll_for_pending_formatter_switch();

                try_resync_clock();

                if (poll_threads())
                {
                    // Reset backoff if there were events to process
                    backoff_us = 1;
                }
                else
                {
                    // Double the backoff each time there were no events, up to
                    // max_backoff
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(backoff_us));
                    backoff_us = std::min(backoff_us * 2, max_backoff_us);
                }
            }

            // Poll remaining events until all thread queues are emptied and
            // deleted
            while (_head.load(std::memory_order_acquire) != nullptr)
            {
                try_resync_clock();
                poll_threads();
            }
        });
    }

  private:
    // Holds a local copy of the raw message data in a thread queue
    std::byte* _queue_copy = new std::byte[1];
    size_t _queue_copy_size = 1;

    std::atomic<bool> _running = false;
    std::atomic<ThreadContextNode*> _head = nullptr;
    SinkHandler _sink;
    PatternFormatterHandler _pattern_formatter;
    std::atomic<LogLevel> min_log_level = LogLevel::Trace;
    Clock _clock;
    std::thread _backend_thread;
};

class ThreadContextHandler
{
  public:
    ThreadContextHandler(std::string_view thread_name, const size_t queue_size)
        : _context{ new ThreadContext{ thread_name, queue_size } }
    {
        Backend::get_or_create_instance().register_thread(_context);
    }

    ~ThreadContextHandler()
    {
        // Don't delete the context here, just mark it as dead and
        // let the background thread delete it when all remaining queue events
        // have been read
        _context->kill();
    }

    Queue& get_queue()
    {
        return _context->get_queue();
    }

    const char* get_name()
    {
        return _context->get_name();
    }

  private:
    ThreadContext* _context;
};

inline std::pair<const char*, Queue*>
get_or_create_thread_queue(std::string_view thread_name = "",
                           const size_t queue_size = defaults::queue_size)
{
    static thread_local ThreadContextHandler context_handler{ thread_name,
                                                              queue_size };
    return { context_handler.get_name(), &context_handler.get_queue() };
}

template <typename... Args>
void push_log_event(const CallSiteStaticData* static_data, Args&&... args)
{
    static thread_local constinit std::pair<const char*, Queue*> t{};
    if (t.second == nullptr) [[unlikely]]
    {
        t = get_or_create_thread_queue();
    }
    auto& [thread_name, q] = t;

    size_t total_args_size = 0;
    if constexpr (sizeof...(args) > 0)
    {
        total_args_size
            = (Codec<std::remove_cvref_t<Args>>::encoded_size(args) + ...);
    }
    size_t header_plus_args_size = total_args_size + sizeof(LogEventHeader);

    // TODO: If the cpu doesn't have invariant tsc, we have to fall back to
    // chrono, so must figure out how to handle that
    const uint64_t timestamp = read_tsc();

    std::byte* buffer = q->reserve_write(header_plus_args_size);
    if (buffer == nullptr)
        return; // Not enough queue space, drop the event

    Codec<const CallSiteStaticData*>::encode(buffer, static_data);
    Codec<const char*>::encode(buffer, thread_name);
    Codec<uint64_t>::encode(buffer, timestamp);
    Codec<size_t>::encode(buffer, total_args_size);
    // Encode args
    ((Codec<std::remove_cvref_t<Args>>::encode(buffer, args)), ...);

    q->commit_write();
}

} // end namespace papper::detail

// clang-format off
#define PAPPER_LOG_IMPL(level, fmt_str, ...)                                \
{                                                                           \
    static constexpr papper::detail::CallSiteStaticData static_data = {     \
        level,                                                              \
        fmt_str,                                                            \
        decltype(papper::detail::get_decoding_function(__VA_ARGS__))::value,\
        std::source_location::current(),                                    \
    };                                                                      \
    papper::detail::push_log_event(&static_data __VA_OPT__(,) __VA_ARGS__); \
}
// clang-format on
