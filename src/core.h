#ifndef _CORE_H_
#define _CORE_H_

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>

#include <string>
#include <thread>
#include <vector>

#include "codec.h"
#include "queue.h"

namespace papper::core
{

using namespace codec;
using namespace queue;

namespace defaults
{

inline size_t queue_size = 1048576; // 1Mb

}

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

struct LogEventHeader
{
    const char* fmt_str = "";
    size_t payload_size;
    void (*decoding_fn)(const char*, const std::byte*, std::string&);
};

class ThreadContext
{
  public:
    ThreadContext(const size_t queue_size)
        : _q{ queue_size }, _is_alive{ true }
    {
    }

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

        // Apply any pending sink that wasn't picked up before shutdown
        switch_to_new_sink();

        if (_sink)
        {
            fflush(_sink);
            if (_sink != stdout && _sink != stderr)
            {
                fclose(_sink);
            }
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

    void set_sink(FILE* sink)
    {
        if (sink == nullptr)
            return;

        // Set _new_sink and if there already was a value there, close that
        // previous one
        FILE* prev_new = _new_sink.exchange(sink, std::memory_order_acq_rel);
        if (prev_new != nullptr && prev_new != stdout && prev_new != stderr)
        {
            fclose(prev_new);
        }
    }

  private:
    void switch_to_new_sink()
    {
        FILE* new_sink
            = _new_sink.exchange(nullptr, std::memory_order_acq_rel);
        if (new_sink != nullptr)
        {
            fflush(_sink);
            if (_sink != stdout && _sink != stderr)
            {
                fclose(_sink);
            }
            _sink = new_sink;
            if (_sink == stdout || _sink == stderr)
                setvbuf(_sink, nullptr, _IOLBF, 65536);
            else
                setvbuf(_sink, nullptr, _IOFBF, 65536);
        }
    }

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

        buffer = q.reserve_read(header.payload_size);
        std::string formatted_output;
        header.decoding_fn(header.fmt_str, buffer, formatted_output);
        q.commit_read();

        fwrite(formatted_output.data(), 1, formatted_output.size(), _sink);
        fputc('\n', _sink);

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

            n_events_processed += 1;
            node = next;
        }

        return n_events_processed;
    }

    Backend()
    {
        // setvbuf(_sink, nullptr, _IOFBF, 65536);
        setvbuf(_sink, nullptr, _IOLBF, 65536);

        _running.store(true, std::memory_order_relaxed);
        _backend_thread = std::thread([this]() {
            uint64_t backoff_Ms = 1;
            // TODO: Interface for user to change this
            constexpr uint64_t max_backoff_Ms = 1000;

            while (_running.load(std::memory_order_acquire))
            {
                switch_to_new_sink();

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
                switch_to_new_sink();
                poll_threads_once();
            }
        });
    }

  private:
    std::atomic<bool> _running = false;
    std::atomic<FILE*> _new_sink = nullptr;
    FILE* _sink
        = stdout; // Only accessed by the backend thread (and dtor after join)
    std::atomic<ThreadContextNode*> _head = nullptr;
    std::thread _backend_thread;
};

class ThreadContextHandler
{
  public:
    ThreadContextHandler(const size_t queue_size)
        : _context{ new ThreadContext{ queue_size } }
    {
        Backend::get_or_create_instance().register_thread(_context);
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

inline Queue& get_or_create_thread_queue(const size_t queue_size
                                         = defaults::queue_size)
{
    static thread_local ThreadContextHandler context_handler(queue_size);
    return context_handler.get_queue();
}

}

#endif
