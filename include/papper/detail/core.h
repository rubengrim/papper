#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <fmt/compile.h>
#include <fmt/format.h>

#include "codec.h"
#include "control_events.h"
#include "globals.h"
#include "level.h"
#include "pattern.h"
#include "platform.h"
#include "queue.h"
#include "sink.h"
#include "static_data_ref.h"
#include "time.h"

#if defined(PAPPER_LINUX)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <sched.h>
#endif

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

template <>
struct Codec<LogEventHeader>
{
    using DecodedT = LogEventHeader;

    static size_t encoded_size(const LogEventHeader&)
    {
        return sizeof(LogEventHeader);
    }

    // LogEventHeader is always encoded element by element for performance
    // reasons, so this function is never called
    static void encode(std::byte*& buffer, const LogEventHeader& arg,
                       const size_t size)
    {
        std::memcpy(buffer, &arg, size);
        buffer += size;
    }

    static void decode(const std::byte*& buffer, DecodedT& arg)
    {
        std::memcpy(&arg, buffer, sizeof(LogEventHeader));
        buffer += sizeof(LogEventHeader);
    }
};

template <typename T>
using decoded_t = typename Codec<T>::DecodedT;

// Formats log arguments into the output buffer
// Returns void but is marked auto so clangd can't skip it and lose in-editor
// format string/argument validation
template <typename CompiledFmtStr, typename... Args>
auto decode_and_format(fmt::memory_buffer& out, const std::byte*& args_data)
{
    std::tuple<decoded_t<Args>...> args;

    // Decode arguments into tuple
    std::apply(
        [&](auto&... arg) { (Codec<Args>::decode(args_data, arg), ...); },
        args);

    std::apply(
        [&](auto&... v) {
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

// Small wrapper around a buffer, how many bytes it contains, and how much has
// been read from it
// Stored in the ThreadContext so the backend can copy the frontend queue and
// read from this instead
struct QueueCopyBuffer
{
    QueueCopyBuffer(const size_t capacity)
    {
        buffer = new std::byte[capacity];
        memset(buffer, 0, capacity);
    }

    ~QueueCopyBuffer()
    {
        delete[] buffer;
    }

    QueueCopyBuffer(QueueCopyBuffer&) = delete;
    QueueCopyBuffer& operator=(QueueCopyBuffer&) = delete;

    // One or more events from the queue are copied into the copy buffer each
    // poll iteration
    std::byte* buffer;
    // Contains the total size of all events currently in the copy buffer
    size_t bytes_contained = 0;
    size_t read_pos = 0;
};

class ThreadContext
{
  public:
    ThreadContext(const size_t queue_size)
        : _is_alive{ true }, _q{ queue_size },
          _copy_buffer{ _q.get_physical_capacity() }
    {

        static std::atomic<size_t> thread_counter = 0;
        _id = ++thread_counter;
        _names.emplace_back("thread_" + std::to_string(_id));
    }

    Queue& get_queue()
    {
        return _q;
    }

    QueueCopyBuffer& get_queue_copy_buffer()
    {
        return _copy_buffer;
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

    std::atomic<ThreadControlEventStatus>* get_ctrl_event_status()
    {
        return &_ctrl_event_status;
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
    std::atomic<bool> _is_alive;
    std::atomic<ThreadControlEventStatus> _ctrl_event_status
        = ThreadControlEventStatus::NotQueued;
    Queue _q;
    QueueCopyBuffer _copy_buffer;
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
        {
            return;
        }

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
    void set_sleep_duration(const std::chrono::system_clock::duration duration)
    {
        _sleep_duration = duration;
    }

    bool try_pin_thread(int cpu_id)
    {
        // Thread pinning is not a thing on mac, so this function only does
        // something on linux (even openBSD and other non-linux unix platforms
        // don't support pinning I think)
        // Windows has pinning but is not supported by papper yet
#if defined(PAPPER_LINUX)
        if (cpu_id < 0 || cpu_id >= CPU_SETSIZE)
        {
            return false;
        }

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);

        return pthread_setaffinity_np(
                   pthread_self(), sizeof(cpu_set_t), &cpuset)
               == 0;
#else
        (void)cpu_id;
        return false;
#endif
    }

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

        std::atomic<ThreadControlEventStatus>* ctrl_event_status;
        Codec<StaticDataRef<std::atomic<ThreadControlEventStatus>>>::decode(
            buffer, ctrl_event_status);

        bool succeeded = true;
        switch (type)
        {
        case ControlEventType::Sink: {
            const SinkEvent sink_switch = event.sink;
            _sink.set_new_sink(sink_switch.file);
            break;
        }
        case ControlEventType::FlushBehaviour: {
            const FlushBehaviourEvent flush = event.flush_behaviour;
            _sink.set_flush_behaviour(
                flush.mode, flush.buffer_size, flush.max_flush_interval_ms);
            break;
        }
        case ControlEventType::Pattern: {
            std::unique_ptr<const PatternFormatterBase> formatter{
                event.pattern.formatter
            };
            _sink.set_new_pattern_formatter(std::move(formatter));
            break;
        }
        case ControlEventType::Colors: {
            std::unique_ptr<const ColorConfig> colors{ event.colors.colors };
            _sink.set_new_colors(std::move(colors));
            break;
        }
        case ControlEventType::ColorsEnable: {
            _sink.set_colors_enabled(event.colors_enabled.enabled);
            break;
        }
        case ControlEventType::CPUAffinity: {
            succeeded = try_pin_thread(event.cpu_affinity.cpu_id);
            break;
        }
        case ControlEventType::BackendSleepDuration: {
            set_sleep_duration(event.sleep_duration.duration);
            break;
        }
        }

        if (succeeded)
        {
            ctrl_event_status->store(ThreadControlEventStatus::Succeeded,
                                     std::memory_order_release);
        }
        else
        {
            ctrl_event_status->store(ThreadControlEventStatus::Failed,
                                     std::memory_order_release);
        }
    }

    // Reads and processes one single event from a buffer.
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
                   + sizeof(ControlEvent) + sizeof(std::atomic<bool>*);
        }

        const size_t total_event_size
            = sizeof(LogEventHeader) + header.payload_size;

        _log_message_formatting_buffer.clear();
        header.static_data->decoding_fn(_log_message_formatting_buffer,
                                        buffer);

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
            .message = std::string_view(_log_message_formatting_buffer.data(),
                                        _log_message_formatting_buffer.size()),
        };
        _sink.format_and_buffer(message_data);

        return total_event_size;
    }

    // Poll all threads for events, processes them, and returns true if there
    // were any events
    bool poll_threads()
    {

        ThreadContextNode* node = _head.load(std::memory_order_acquire);
        while (node != nullptr)
        {
            Queue& q = node->context->get_queue();
            QueueCopyBuffer& q_copy = node->context->get_queue_copy_buffer();

            Queue::MultipleEventReadReservation read
                = q.reserve_read_many_continuous();
            if (read.buffer != nullptr)
            {
                // Make a copy of the events in the queue and commit the read
                // immediately
                std::memcpy(q_copy.buffer, read.buffer, read.size);
                q.commit_multiple_event_read(read.new_read_pos);

                q_copy.bytes_contained = read.size;
                q_copy.read_pos = 0;
            }
            else
            {
                q_copy.bytes_contained = 0;
                q_copy.read_pos = 0;
            }

            node = node->next;
        }

        bool processed_events = false;

        // Read from the queue copies round-robin until they are all empty
        bool processed_events_this_iter = false;
        do
        {
            processed_events_this_iter = false;
            node = _head.load(std::memory_order_acquire);

            while (node != nullptr)
            {
                ThreadContextNode* next = node->next;

                QueueCopyBuffer& q_copy
                    = node->context->get_queue_copy_buffer();

                // If there is more to read, read and process a single event
                // from the queue copy
                if (q_copy.read_pos < q_copy.bytes_contained)
                {
                    const size_t event_size = read_and_process_single_event(
                        &q_copy.buffer[q_copy.read_pos]);
                    q_copy.read_pos += event_size;

                    processed_events_this_iter = true;
                    processed_events = true;
                }
                else if (!node->context->thread_is_alive()
                         && node->context->get_queue().is_empty())
                {
                    // Delete and remove the node+context from the
                    // list if the thread has been killed and there
                    // are no events left in the queue to process
                    remove_and_delete_node(node);
                }

                node = next;
            }
        } while (processed_events_this_iter);

        return processed_events;
    }

    // Drains every event that was send up to when shutdown was called
    void drain_pre_shutdown_events()
    {
        ThreadContextNode* node = _head.load(std::memory_order_acquire);
        while (node != nullptr)
        {
            Queue& q = node->context->get_queue();
            QueueCopyBuffer& q_copy = node->context->get_queue_copy_buffer();
            q_copy.bytes_contained = 0;
            q_copy.read_pos = 0;

            // Copy all events out of the queue
            // Since the events could be discontinous in memory (i.e. the first
            // part is stored at the end of the queue and the second part
            // wrapped and is stored at the front), this needs to happen in two
            // reads
            // The events will be placed continously in q_copy no matter their
            // placement in the queue
            Queue::MultipleEventReadReservation read
                = q.reserve_read_many_continuous();
            if (read.buffer != nullptr)
            {
                std::memcpy(q_copy.buffer, read.buffer, read.size);
                q.commit_multiple_event_read(read.new_read_pos);
                q_copy.bytes_contained = read.size;

                // If the first read succeeded, try reading again in case the
                // events lie discontinuously in memory
                read = q.reserve_read_many_continuous();
                if (read.buffer != nullptr)
                {
                    std::memcpy(&q_copy.buffer[q_copy.bytes_contained],
                                read.buffer,
                                read.size);
                    q.commit_multiple_event_read(read.new_read_pos);
                    q_copy.bytes_contained += read.size;
                }
            }

            node = node->next;
        }

        // Read from the queue copies round-robin until they are all empty
        bool processed_events_this_iter = false;
        do
        {
            processed_events_this_iter = false;
            node = _head.load(std::memory_order_acquire);

            while (node != nullptr)
            {
                QueueCopyBuffer& q_copy
                    = node->context->get_queue_copy_buffer();

                // If there is more to read, read and process a single event
                // from the queue copy
                if (q_copy.read_pos < q_copy.bytes_contained)
                {
                    const size_t event_size = read_and_process_single_event(
                        &q_copy.buffer[q_copy.read_pos]);
                    q_copy.read_pos += event_size;

                    processed_events_this_iter = true;
                }

                // No node/thread context removal happens here because the
                // process will exit soon anyway and free the memory

                node = node->next;
            }
        } while (processed_events_this_iter);
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

        set_backend_state(State::Running);

        _backend_thread = std::thread([this]() {
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

            // Main backend loop:
            // 1. Check if the clock should be recalibrated
            // 2. Process every event currently in each thread queue
            // 3. Sleep for some duration
            while (get_backend_state() == State::Running)
            {
                poll_clock_resync();
                poll_threads();
                std::this_thread::sleep_for(_sleep_duration);
            }
        });

        std::atexit([]() { get_or_create_instance().shutdown(); });
    }

  private:
    std::atomic<State> _state = State::NotStarted;
    std::atomic<ThreadContextNode*> _head = nullptr;
    SinkHandler _sink;
    Clock _clock;
    fmt::memory_buffer _log_message_formatting_buffer;
    std::atomic<LogLevel> _min_log_level = LogLevel::Trace;
    std::chrono::system_clock::duration _sleep_duration
        = std::chrono::microseconds(100);
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
// Returns whether or not processing of the event succeeded on the backend
// A false return value can also mean that the event could not be sent
inline bool push_control_event(const ControlEventType type,
                               const ControlEvent& event)
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
        return false;
    }

    Queue& q = context->get_queue();

    const size_t write_size = sizeof(LogEventHeader) + sizeof(ControlEventType)
                              + sizeof(ControlEvent)
                              + sizeof(std::atomic<ThreadControlEventStatus>*);

    std::byte* buffer = nullptr;
    Queue::SingleEventWriteReservation write
        = Queue::SingleEventWriteReservation{};
    // Block if the queue is full and retry until we have our write block
    while (buffer == nullptr)
    {
        write = q.reserve_write(write_size);
        buffer = write.buffer;

        // Check if backend is shut down or we'll spin forever
        if (Backend::get_or_create_instance().is_shutdown_or_shutting_down())
            [[unlikely]]
        {
            return false;
        }
    }

    // Static data being nullptr signals that this is not a log event
    Codec<void*>::encode(buffer, nullptr, sizeof(void*));
    Codec<void*>::encode(buffer, nullptr, sizeof(void*));
    Codec<uint64_t>::encode(buffer, 0, sizeof(uint64_t));
    Codec<size_t>::encode(buffer, 0, sizeof(size_t));

    Codec<ControlEventType>::encode(buffer, type, sizeof(ControlEventType));
    Codec<ControlEvent>::encode(buffer, event, sizeof(ControlEvent));

    std::atomic<ThreadControlEventStatus>* status
        = context->get_ctrl_event_status();
    status->store(ThreadControlEventStatus::InQueue,
                  std::memory_order_release);
    Codec<StaticDataRef<std::atomic<ThreadControlEventStatus>>>::encode(
        buffer, StaticDataRef(status), sizeof(void*));

    q.commit_write(write.new_write_pos);

    // Block until the backend has processed the event
    while (status->load(std::memory_order_acquire)
           == ThreadControlEventStatus::InQueue)
    {
    }

    const bool succeeded = status->load(std::memory_order_acquire)
                           == ThreadControlEventStatus::Succeeded;
    status->store(ThreadControlEventStatus::NotQueued,
                  std::memory_order_release);

    // Return whether or not processing of the control event succeeded
    return succeeded;
};

template <bool Block, typename... Args>
constexpr void push_log_event(const CallSiteStaticData* static_event_data,
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

    // To get rid of warnings, clamp size_store to length 1 when there are no
    // arguments
    constexpr size_t n_args_clamped
        = sizeof...(Args) >= 1 ? sizeof...(Args) : 1;
    size_t size_store[n_args_clamped];
    size_t size_store_idx = 0;

    auto push_to_store
        = [&size_store, &size_store_idx](size_t arg_size) -> size_t {
        size_store[size_store_idx++] = arg_size;
        return arg_size;
    };

    // Count the total encoded argument size, and save each argument's size in
    // size_store
    size_t total_args_size = 0;
    if constexpr (sizeof...(args) > 0)
    {
        total_args_size
            = (push_to_store(
                   Codec<std::remove_cvref_t<Args>>::encoded_size(args))
               + ...);
    }
    size_t header_plus_args_size = total_args_size + sizeof(LogEventHeader);

    // TODO: If the cpu doesn't have invariant tsc, we have to fall back to
    // chrono, so must figure out how to handle that
    const uint64_t timestamp = read_tsc();

    Queue::SingleEventWriteReservation write
        = q.reserve_write(header_plus_args_size);
    std::byte* buffer = write.buffer;

    if constexpr (Block)
    {
        // Block until there is space in queue
        while (buffer == nullptr)
        {
            write = q.reserve_write(header_plus_args_size);
            buffer = write.buffer;

            // Check if backend is shut down or we'll spin forever
            if (Backend::get_or_create_instance()
                    .is_shutdown_or_shutting_down()) [[unlikely]]
            {
                return;
            }
        }
    }
    else
    {
        if (buffer == nullptr)
            return; // Not enough queue space, drop the event
    }

    Codec<StaticDataRef<const CallSiteStaticData>>::encode(
        buffer, StaticDataRef(static_event_data), sizeof(CallSiteStaticData*));
    // Since thread_name is a char* with static lifetime it can be encoded
    // through a StaticDataRef to just copy the pointer and bypass strlen and
    // having to memcpy the whole string
    Codec<StaticDataRef<const char>>::encode(
        buffer, StaticDataRef(thread_name), sizeof(char*));
    Codec<uint64_t>::encode(buffer, timestamp, sizeof(uint64_t));
    Codec<size_t>::encode(buffer, total_args_size, sizeof(size_t));

    size_store_idx = 0;
    auto encode_arg = [&buffer, &size_store, &size_store_idx]<typename ArgT>(
                          const ArgT& arg) {
        const size_t size = size_store[size_store_idx++];
        Codec<std::remove_cvref_t<ArgT>>::encode(buffer, arg, size);
    };
    (encode_arg(args), ...);

    q.commit_write(write.new_write_pos);
}

} // end namespace papper::detail

// clang-format off

#define PAPPER_LOG_IMPL(block, level, fmt_str, ...)                         \
{                                                                           \
    static constexpr auto compiled_fmt_str = FMT_COMPILE(fmt_str);          \
    static constexpr papper::detail::CallSiteStaticData static_data = {     \
        level,                                                              \
        decltype(papper::detail::get_decoding_function(                     \
            compiled_fmt_str __VA_OPT__(,) __VA_ARGS__))::value,            \
        std::source_location::current(),                                    \
    };                                                                      \
    papper::detail::push_log_event<block>(                                  \
            &static_data __VA_OPT__(,) __VA_ARGS__);                        \
}

// clang-format on
