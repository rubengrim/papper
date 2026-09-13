#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>

#include <mutex>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

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
std::string decode_and_format(const char* fmt_str, const std::byte* args_data)
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
            return std::vformat(fmt_str, std::make_format_args(v...));
        },
        args);
}

template <typename Tuple>
struct DecodingFunction;

template <typename... Args>
struct DecodingFunction<std::tuple<Args...>>
{
    static constexpr auto fn
        = &decode_and_format<std::remove_cvref_t<Args>...>;
};

struct CallSiteStaticData
{
    LogLevel level;
    const char* fmt_str = "";
    std::string (*decoding_fn)(const char*, const std::byte*);
    std::source_location location;
};

struct LogEventHeader
{
    const CallSiteStaticData* static_data;
    const char* thread_name;
    uint64_t timestamp;
    size_t payload_size;
};

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

    bool try_process_log_event(Queue& q)
    {
        const std::byte* buffer = q.reserve_read(sizeof(LogEventHeader));
        if (buffer == nullptr)
            return false; // Nothing to read

        LogEventHeader header;
        Codec<LogEventHeader>::decode(buffer, header);
        q.commit_read();

        if (header.static_data->level
            < min_log_level.load(std::memory_order_acquire))
            return true; // Drop the event if to low level

        buffer = q.reserve_read(header.payload_size);
        std::string message = header.static_data->decoding_fn(
            header.static_data->fmt_str, buffer);
        q.commit_read();

        PatternData pattern_data = {
            .level = header.static_data->level,
            .thread_name = header.thread_name,
            .timestamp
            = _timestamp_converter.timestamp_to_system_time(header.timestamp),
            .filename = header.static_data->location.file_name(),
            .functionname = header.static_data->location.function_name(),
            .linenumber = header.static_data->location.line(),
            .message = message,
        };
        std::string prefix = _pattern_formatter.format(pattern_data);

        _sink.write_str_and_endl(prefix);

        return true;
    }

    int poll_threads_once()
    {
        int n_events_processed = 0;
        ThreadContextNode* node = _head.load(std::memory_order_acquire);
        while (node != nullptr)
        {
            ThreadContextNode* next = node->next;

            if (!try_process_log_event(node->context->get_queue()))
            {
                if (!node->context->thread_is_alive())
                {
                    // Delete and remove the node+context from the
                    // list if the thread has been killed and there
                    // are no events left in the queue to process
                    remove_and_delete_node(node);
                }
            }
            else
            {
                n_events_processed += 1;
            }

            node = next;
        }

        return n_events_processed;
    }

    Backend()
    {
        if (!_timestamp_converter.init())
        {
            throw std::runtime_error(
                "CPU does not have an invariant TSC. "
                "Fallback to std::chrono::steady_clock "
                "is no implemented yet, so papper can't run on this machine.");
        }

        _running.store(true, std::memory_order_relaxed);
        _backend_thread = std::thread([this]() {
            uint64_t backoff_Ms = 1;
            // TODO: Interface for user to change this
            constexpr uint64_t max_backoff_Ms = 1000;

            while (_running.load(std::memory_order_acquire))
            {
                _sink.poll_for_pending_sink_switch();
                _pattern_formatter.poll_for_pending_formatter_switch();
                _timestamp_converter
                    .sync_to_system_clock(); // Do this less often?

                if (poll_threads_once() > 0)
                {
                    // Reset backoff if there were events to process
                    backoff_Ms = 1;
                }
                else
                {
                    // Double the backoff each time there were no events, up to
                    // max_backoff
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(backoff_Ms));
                    backoff_Ms = std::min(backoff_Ms * 2, max_backoff_Ms);
                }
            }

            // Poll remaining events until all thread queues are emptied and
            // deleted
            while (_head.load(std::memory_order_acquire) != nullptr)
            {
                poll_threads_once();
            }
        });
    }

  private:
    std::atomic<bool> _running = false;
    std::atomic<ThreadContextNode*> _head = nullptr;
    SinkHandler _sink;
    PatternFormatterHandler _pattern_formatter;
    std::atomic<LogLevel> min_log_level = LogLevel::Trace;
    Clock _timestamp_converter;
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
    static thread_local auto [thread_name, q] = get_or_create_thread_queue();

    size_t total_args_size = 0;
    if constexpr (sizeof...(args) > 0)
    {
        total_args_size
            = (Codec<std::remove_cvref_t<Args>>::encoded_size(args) + ...);
    }
    size_t header_plus_args_size = total_args_size + sizeof(LogEventHeader);

    LogEventHeader header;
    header.static_data = static_data;
    header.thread_name = thread_name;
    // TODO: If the cpu doesn't have invariant tsc, we have to fall back to
    // chrono, so must figure out how to handle that
    header.timestamp = read_tsc();
    header.payload_size = total_args_size;

    std::byte* buffer = q->reserve_write(header_plus_args_size);
    if (buffer == nullptr)
        return; // Not enough queue space, drop the event

    // Encode header
    Codec<LogEventHeader>::encode(buffer, header);
    // Encode args
    ((Codec<std::remove_cvref_t<Args>>::encode(buffer, args)), ...);

    q->commit_write();
}

} // end namespace papper::detail

// clang-format off
#define log_impl(level, fmt_str, ...)                                       \
{                                                                           \
    static constexpr papper::detail::CallSiteStaticData static_data = {     \
        level,                                                              \
        fmt_str,                                                            \
        papper::detail::DecodingFunction<                                   \
            decltype(std::forward_as_tuple(__VA_ARGS__))>::fn,              \
        std::source_location::current(),                                    \
    };                                                                      \
    papper::detail::push_log_event(&static_data __VA_OPT__(,) __VA_ARGS__); \
}
// clang-format on
