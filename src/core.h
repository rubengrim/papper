#ifndef _CORE_H_
#define _CORE_H_

#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "codec.h"
#include "queue.h"

struct LogEventHeader
{
    const char* fmt_str = "";
    size_t payload_size;
    void (*decoding_fn)(const char*, const std::byte*, std::string&);
};

template <typename... Args>
void decode_and_format(const char* fmt_str, const std::byte* args_data,
                       std::string& formatted_output)
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
    std::apply(
        [&](auto&&... v) {
            formatted_output
                = std::vformat(fmt_str, std::make_format_args(v...));
        },
        args);
}

class ThreadContext
{
  public:
    ThreadContext() : _q{ 100000 }, _is_alive{ true } {}

    Queue& get_queue()
    {
        return _q;
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
    Queue _q;
    std::atomic<bool> _is_alive;
};

struct ThreadContextNode
{
    ThreadContext* context;
    ThreadContextNode* next{ nullptr };
};

class LogBackend
{
  public:
    static LogBackend& get_instance()
    {
        static LogBackend instance{};
        return instance;
    }

    LogBackend(const LogBackend&) = delete;
    LogBackend(LogBackend&&) = delete;
    LogBackend& operator=(const LogBackend&) = delete;
    LogBackend& operator=(LogBackend&&) = delete;

    ~LogBackend()
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

  private:
    static bool try_process_log_event(Queue& q)
    {
        const std::byte* buffer = q.reserve_read(sizeof(LogEventHeader));
        if (buffer == nullptr)
            return false; // Nothing to read

        LogEventHeader header;
        Codec<LogEventHeader>::decode(buffer, header);
        q.commit_read();

        buffer = q.reserve_read(header.payload_size);
        std::string formatted_output;
        header.decoding_fn(header.fmt_str, buffer, formatted_output);
        q.commit_read();

        std::cout << formatted_output << std::endl;
        return true;
    }

    // May ONLY be called by backend thread
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

                // Nothing more to read from this queue
                break;
            }

            n_events_processed += 1;
            node = next;
        }

        return n_events_processed;
    }

    LogBackend()
    {
        _backend_thread = std::thread([this]() {
            uint64_t backoff_Ms = 1;
            constexpr uint64_t max_backoff_Ms = 1000;

            while (_running.load(std::memory_order_acquire))
            {
                if (poll_threads_once() > 0)
                {
                    // Reset backoff if there were events to process
                    backoff_Ms = 1;
                }
                else
                {
                    // Double backoff each time there were no events up to
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
    std::atomic<bool> _running{ true };
    std::mutex _mutex;
    std::atomic<ThreadContextNode*> _head{ nullptr };
    std::thread _backend_thread;
};

class ThreadContextHandler
{
  public:
    ThreadContextHandler() : _context{ new ThreadContext }
    {
        LogBackend::get_instance().register_thread(_context);
    }

    ~ThreadContextHandler()
    {
        // Do not actually delete the context here, just mark it as dead and
        // let the background thread delete it when all remaining queue events
        // have been read
        _context->kill();
    }

    Queue& get_queue()
    {
        return _context->get_queue();
    }

  private:
    ThreadContext* _context;
};

inline Queue& get_thread_queue()
{
    static thread_local ThreadContextHandler context_handler;
    return context_handler.get_queue();
}

template <typename... Args>
void log(const char* fmt_str, Args&&... args)
{
    Queue& q = get_thread_queue();

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

#endif
