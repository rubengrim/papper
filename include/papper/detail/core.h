#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include <fmt/compile.h>
#include <fmt/format.h>

#include "codec.h"
#include "control_events.h"
#include "globals.h"
#include "level.h"
#include "pattern.h"
#include "queue.h"
#include "sink.h"
#include "time.h"

namespace papper::detail
{

struct CallSiteStaticData
{
    LogLevel level;
    void (*decoding_fn)(fmt::memory_buffer&, const std::byte*&);
    std::source_location location;
};

struct LogEventHeader
{
    const CallSiteStaticData* static_data;
    const char* thread_name;
    uint64_t timestamp;
    size_t payload_size;
};

// Check that the header isn't padded, since it's encoded field by field into
// the buffer, but decoded as a whole
static_assert(sizeof(LogEventHeader)
              == sizeof(CallSiteStaticData*) + sizeof(const char*)
                     + sizeof(uint64_t) + sizeof(size_t));

template <typename CompiledFmtStr, typename... Args>
void decode_and_format(fmt::memory_buffer& out, const std::byte*& args_data)
{
    using ArgsTuple = std::tuple<Args...>;
    ArgsTuple args;

    // Decode arguments into tuple
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (Codec<std::tuple_element_t<Is, ArgsTuple>>::decode(
             args_data, std::get<Is>(args)),
         ...);
    }(std::index_sequence_for<Args...>{});

    std::apply(
        [&](auto&&... v) {
            return fmt::format_to(fmt::appender(out), CompiledFmtStr{}, v...);
        },
        args);
}

template <typename CompiledFmtStr, typename... Args>
struct DecodingFunction
{
    static constexpr auto value = &decode_and_format<CompiledFmtStr, Args...>;
};

// Wrapper so that decode_and_format can be specialized from a macro
template <typename CompiledFmtStr, typename... Args>
DecodingFunction<CompiledFmtStr, std::remove_cvref_t<Args>...>
get_decoding_function(CompiledFmtStr, Args&&...);

class ThreadContext
{
  public:
    ThreadContext(const size_t queue_size)
        : _q{ queue_size }, _is_alive{ true }
    {
        static std::atomic<size_t> thread_counter = 0;
        _id = ++thread_counter;
        _names.emplace_back("thread_" + std::to_string(_id));
    }

    Queue& get_queue()
    {
        return _q;
    }

    size_t get_id() const
    {
        return _id;
    }

    const char* get_name() const
    {
        return _names.back().c_str();
    }

    void set_name(std::string_view name)
    {
        _names.emplace_back(name);
    }

    bool thread_is_alive() const
    {
        return _is_alive.load(std::memory_order_acquire);
    }

    void kill()
    {
        _is_alive.store(false, std::memory_order_release);
    }

  private:
    size_t _id;
    // Store history of thread names in case the name is changed while a log
    // event is still in the queue with a pointer to the old name
    std::deque<std::string> _names;
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
        static Backend* instance = new Backend{};
        return *instance;
    }

    Backend(const Backend&) = delete;
    Backend(Backend&&) = delete;
    Backend& operator=(const Backend&) = delete;
    Backend& operator=(Backend&&) = delete;

    enum class State
    {
        NotStarted,
        Running,
        ShuttingDown,
        Shutdown,
    };

    State get_backend_state() const
    {
        return _state.load(std::memory_order_acquire);
    }

    void register_thread(ThreadContext* context)
    {
        if (is_shutdown_or_shutting_down())
            return;

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

    void shutdown()
    {
        if (get_backend_state() == State::Running)
        {
            set_backend_state(State::ShuttingDown);
            if (_backend_thread.joinable())
            {
                _backend_thread.join();
            }

            drain_pre_shutdown_events();
            _sink.flush();

            set_backend_state(State::Shutdown);
        }
    }

    bool is_shutdown_or_shutting_down() const
    {
        State state = get_backend_state();
        return state == State::Shutdown || state == State::ShuttingDown;
    }

    void set_new_minimum_log_level(const LogLevel new_level)
    {
        _min_log_level.store(new_level, std::memory_order_release);
    }

  private:
    void set_backend_state(State state)
    {
        _state.store(state, std::memory_order_release);
    }

    size_t get_num_registered_threads() const
    {
        size_t count = 0;
        ThreadContextNode* node = _head.load(std::memory_order_acquire);
        while (node->next != nullptr)
        {
            count++;
            node = node->next;
        }

        return count;
    }

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

    void read_and_process_control_event(const std::byte* buffer)
    {
        ControlEventType type;
        Codec<ControlEventType>::decode(buffer, type);

        ControlEvent event;
        Codec<ControlEvent>::decode(buffer, event);

        switch (type)
        {
        case ControlEventType::NewSink: {
            const NewSinkEvent sink_switch = event.new_sink;
            _sink.set_new_sink(sink_switch.file);
            return;
        }
        case ControlEventType::NewFlushConfig: {
            const NewFlushBehaviourEvent flush = event.new_flush_behaviour;
            _sink.set_flush_behaviour(
                flush.mode, flush.buffer_size, flush.max_flush_interval_ms);
            return;
        }
        case ControlEventType::NewPattern: {
            std::unique_ptr<const PatternFormatterBase> formatter{
                event.new_pattern.formatter
            };
            _sink.set_new_pattern_formatter(std::move(formatter));
            return;
        }
        case ControlEventType::NewColors: {
            std::unique_ptr<const ColorConfig> colors{
                event.new_colors.colors
            };
            _sink.set_new_colors(std::move(colors));
            return;
        }
        case ControlEventType::ColorEnable: {
            _sink.set_colors_enabled(event.colors_enabled.enabled);
        }
        }
    }

    // Reads and processes one single event from the buffer.
    // Returns the number of bytes that were read
    size_t read_and_process_single_event(const std::byte* buffer)
    {
        LogEventHeader header;
        Codec<LogEventHeader>::decode(buffer, header);

        // nullptr means the event is a control event
        if (header.static_data == nullptr) [[unlikely]]
        {
            read_and_process_control_event(buffer);
            return sizeof(LogEventHeader) + sizeof(ControlEventType)
                   + sizeof(ControlEvent);
        }

        const size_t total_event_size
            = sizeof(LogEventHeader) + header.payload_size;

        _write_buffer.clear();
        header.static_data->decoding_fn(_write_buffer, buffer);

        if (header.static_data->level
            < _min_log_level.load(std::memory_order_acquire))
        {
            return total_event_size; // Drop the event if too low level
        }

        MessageData message_data = {
            .level = header.static_data->level,
            .thread_name = header.thread_name,
            .timestamp
            = _clock.timestamp_to_local_system_time(header.timestamp),
            .filename = header.static_data->location.file_name(),
            .functionname = header.static_data->location.function_name(),
            .linenumber = header.static_data->location.line(),
            .message
            = std::string_view(_write_buffer.data(), _write_buffer.size()),
        };
        _sink.format_and_buffer(message_data);

        return total_event_size;
    }

    // Poll all threads for events, processes them, and returns true if there
    // were any
    bool poll_threads()
    {
        bool were_events_to_process = false;

        ThreadContextNode* node = _head.load(std::memory_order_acquire);
        while (node != nullptr)
        {
            ThreadContextNode* next = node->next;
            Queue& q = node->context->get_queue();

            Queue::ReadReservation read = q.reserve_read();
            if (read.buffer != nullptr)
            {
                const size_t bytes_read
                    = read_and_process_single_event(read.buffer);
                q.commit_read(read.current_read_pos + bytes_read);
                were_events_to_process = true;
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

        return were_events_to_process;
    }

    // Drains every event that was send up to when shutdown was called
    void drain_pre_shutdown_events()
    {
        // Get the current available bytes to read from each thread, so we
        // don't read more than this and risk blocking forever if a background
        // thread has detached and keeps pushing events
        std::unordered_map<size_t, size_t> bytes_to_read;
        ThreadContextNode* node = _head.load(std::memory_order_acquire);
        while (node != nullptr)
        {
            bytes_to_read[node->context->get_id()]
                = node->context->get_queue().n_available_bytes();
            node = node->next;
        }

        while (true)
        {
            bool were_events_to_process = false;

            node = _head.load(std::memory_order_acquire);
            while (node != nullptr)
            {
                const size_t thread_id = node->context->get_id();
                // Check if there's more to read so we only read pre-shutdown
                // events
                if (bytes_to_read[thread_id] > 0)
                {
                    Queue& q = node->context->get_queue();

                    Queue::ReadReservation read = q.reserve_read();
                    if (read.buffer != nullptr)
                    {
                        const size_t bytes_read
                            = read_and_process_single_event(read.buffer);
                        q.commit_read(read.current_read_pos + bytes_read);

                        bytes_to_read[thread_id] -= bytes_read;
                        were_events_to_process = true;
                    }
                }

                node = node->next;
            }

            if (!were_events_to_process)
            {
                break;
            }
        }
    }

    Backend()
    {
        if (!_clock.init())
        {
            throw std::runtime_error(
                "CPU does not have an invariant TSC. "
                "A fallback is not implemented, so papper "
                "can't run on this machine yet.");
        }

        _backend_thread = std::thread([this]() {
            uint32_t backoff_us = 1;
            // TODO: Let user to change this
            const uint32_t max_backoff_us = 200;

            const double resync_utc_offset_interval_ns = 1e6 * 1000.0;
            const double resync_to_system_clock_interval_ns = 1e6 * 100.0;

            uint64_t last_utc_offset_resync = 0;
            uint64_t last_system_clock_resync = 0;

            auto poll_clock_resync = [&]() {
                uint64_t now = read_tsc();
                if (_clock.to_ns(now - last_utc_offset_resync)
                    > resync_utc_offset_interval_ns)
                {
                    _clock.recalculate_utc_offset();
                    last_utc_offset_resync = now;
                }

                now = read_tsc();
                if (_clock.to_ns(now - last_system_clock_resync)
                    > resync_to_system_clock_interval_ns)
                {
                    _clock.sync_to_system_clock();
                    last_system_clock_resync = now;
                }
            };

            set_backend_state(State::Running);
            while (get_backend_state() == State::Running)
            {
                poll_clock_resync();

                if (poll_threads())
                {
                    // Reset backoff if there were events to process
                    backoff_us = 1;
                }
                else // Idle
                {
                    // If idle, poll the sink so messages that don't fill up
                    // the buffer are still flushed periodically
                    _sink.poll_for_flush_timeout(_clock);

                    // Double the backoff each time there were no events, up to
                    // max_backoff
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(backoff_us));
                    backoff_us = std::min(backoff_us * 2, max_backoff_us);
                }
            }
        });

        std::atexit([]() { get_or_create_instance().shutdown(); });
    }

  private:
    std::atomic<State> _state = State::NotStarted;

    std::atomic<ThreadContextNode*> _head = nullptr;
    fmt::memory_buffer _write_buffer;
    SinkHandler _sink;
    std::atomic<LogLevel> _min_log_level = LogLevel::Trace;
    Clock _clock;
    std::thread _backend_thread;
};

class ThreadContextHandler
{
  public:
    ~ThreadContextHandler()
    {
        // Don't delete the context here, just mark it as dead and
        // let the background thread delete it when all remaining queue events
        // have been read
        _context->kill();
        // Set it to a dummy marker so the frontend knows not to
        // send any more messages through it
        _context = reinterpret_cast<ThreadContext*>(
            ThreadContextHandler::_dead_context_marker);
    }

    static ThreadContext* create_context(const size_t queue_size
                                         = globals::default_queue_size)
    {
        // Init the handler as thread_local so its destructor runs when the
        // frontend thread dies
        static thread_local ThreadContextHandler handler{ queue_size };
        return _context;
    }

    static ThreadContext* get_context()
    {
        return _context;
    }

  private:
    ThreadContextHandler(const size_t queue_size)
    {
        _context = new ThreadContext{ queue_size };
        Backend::get_or_create_instance().register_thread(_context);
    }

  public:
    static constexpr std::uintptr_t _dead_context_marker
        = alignof(ThreadContext);

  private:
    inline static thread_local constinit ThreadContext* _context = nullptr;
};

inline ThreadContext* get_or_init_thread_context(const size_t queue_size
                                                 = globals::default_queue_size)
{
    ThreadContext* context = ThreadContextHandler::get_context();
    if (context == nullptr)
    {
        context = ThreadContextHandler::create_context(queue_size);
    }
    else if (reinterpret_cast<std::uintptr_t>(context)
             == ThreadContextHandler::_dead_context_marker) [[unlikely]]
    {
        return nullptr;
    }

    return context;
}

// Pushes a control event for switching sink, pattern or color config to the
// thread queue
inline void push_control_event(const ControlEventType type,
                               const ControlEvent& req)
{
    // Get the thread context, create it if it hasn't been initialized, and
    // drop the message if it's been marked dead
    ThreadContext* context = ThreadContextHandler::get_context();
    if (context == nullptr) [[unlikely]]
    {
        context = ThreadContextHandler::create_context();
    }
    else if (reinterpret_cast<std::uintptr_t>(context)
             == ThreadContextHandler::_dead_context_marker) [[unlikely]]
    {
        return;
    }

    Queue& q = context->get_queue();

    const size_t write_size = sizeof(LogEventHeader) + sizeof(ControlEventType)
                              + sizeof(ControlEvent);

    std::byte* buffer = nullptr;
    Queue::WriteReservation write = Queue::WriteReservation{};
    // Block if the queue is full and retry until we have our write block
    while (buffer == nullptr)
    {
        write = q.reserve_write(write_size);
        buffer = write.buffer;

        // Check if backend is shut down or we'll spin forever
        if (Backend::get_or_create_instance().is_shutdown_or_shutting_down())
            [[unlikely]]
        {
            return;
        }
    }

    // Static data being nullptr signals that this is not a log event
    Codec<const CallSiteStaticData*>::encode(buffer, nullptr);
    Codec<const char*>::encode(buffer, "");
    Codec<uint64_t>::encode(buffer, 0);
    Codec<size_t>::encode(buffer, 0);

    Codec<ControlEventType>::encode(buffer, type);
    Codec<ControlEvent>::encode(buffer, req);

    q.commit_write(write.new_write_pos);
};

template <typename... Args>
inline void push_log_event(const CallSiteStaticData* static_data,
                           Args&&... args)
{
    // Get the thread context, create it if it hasn't been initialized, and
    // drop the message if it's been marked dead
    ThreadContext* context = ThreadContextHandler::get_context();
    if (context == nullptr) [[unlikely]]
    {
        context = ThreadContextHandler::create_context();
    }
    else if (reinterpret_cast<std::uintptr_t>(context)
             == ThreadContextHandler::_dead_context_marker) [[unlikely]]
    {
        return;
    }

    Queue& q = context->get_queue();
    const char* thread_name = context->get_name();

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

    Queue::WriteReservation write = q.reserve_write(header_plus_args_size);
    std::byte* buffer = write.buffer;
    if (buffer == nullptr)
        return; // Not enough queue space, drop the event

    Codec<const CallSiteStaticData*>::encode(buffer, static_data);
    Codec<const char*>::encode(buffer, thread_name);
    Codec<uint64_t>::encode(buffer, timestamp);
    Codec<size_t>::encode(buffer, total_args_size);
    // Encode args
    ((Codec<std::remove_cvref_t<Args>>::encode(buffer, args)), ...);

    q.commit_write(write.new_write_pos);
}

} // end namespace papper::detail

// clang-format off
#define PAPPER_LOG_IMPL(level, fmt_str, ...)                                \
{                                                                           \
    static constexpr auto compiled_fmt_str = FMT_COMPILE(fmt_str);          \
    static constexpr papper::detail::CallSiteStaticData static_data = {     \
        level,                                                              \
        decltype(papper::detail::get_decoding_function(                     \
            compiled_fmt_str __VA_OPT__(,) __VA_ARGS__))::value,            \
        std::source_location::current(),                                    \
    };                                                                      \
    papper::detail::push_log_event(&static_data __VA_OPT__(,) __VA_ARGS__); \
}
// clang-format on
