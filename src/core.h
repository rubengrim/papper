#ifndef _CORE_H_
#define _CORE_H_

#include <atomic>
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

template <typename QueueType, typename... Args>
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

template <typename QueueType>
struct ThreadContext;

template <typename QueueType>
class ThreadContextWrapper;

template <typename QueueType>
class Logger
{
  public:
    static Logger& get_instance()
    {
        static Logger instance{};
        return instance;
    }

    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger& operator=(Logger&&) = delete;

    ~Logger()
    {
        _running.store(false, std::memory_order_release);
        if (poll_thread.joinable())
        {
            poll_thread.join();
        }
    }

  public:
    void register_thread(std::shared_ptr<ThreadContext<QueueType>> context)
    {
        std::lock_guard<std::mutex> guard{ _mutex };
        _contexts.push_back(context);
    }

  private:
    static bool process_log(QueueType& q)
    {
        const std::byte* buffer = q.reserve_read(sizeof(LogEventHeader));
        if (buffer == nullptr)
        {
            return false;
        }

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

    Logger()
    {
        poll_thread = std::thread([this]() {
            while (_running.load(std::memory_order_acquire))
            {
                bool processed = false;
                {
                    std::lock_guard<std::mutex> guard{ _mutex };
                    for (auto it = _contexts.begin(); it != _contexts.end();)
                    {
                        if (process_log((*it)->q))
                        {
                            processed = true;
                            ++it;
                        }
                        else if (!(*it)->is_alive.load(
                                     std::memory_order_acquire))
                        {
                            it = _contexts.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }
                }
                if (!processed)
                {
                    std::this_thread::yield();
                }
            }

            // Drain remaining logs upon shutdown
            bool has_more = true;
            while (has_more)
            {
                has_more = false;
                std::lock_guard<std::mutex> guard{ _mutex };
                for (auto& context : _contexts)
                {
                    while (process_log(context->q))
                    {
                        has_more = true;
                    }
                }
            }
        });
    }

  private:
    std::atomic<bool> _running{ true };
    std::mutex _mutex;
    std::vector<std::shared_ptr<ThreadContext<QueueType>>> _contexts;
    std::thread poll_thread;
};

template <typename QueueType>
struct ThreadContext
{
    QueueType q;
    std::atomic<bool> is_alive{ true };
};

template <typename QueueType>
class ThreadContextWrapper
{
  public:
    ThreadContextWrapper()
        : _context{ std::make_shared<ThreadContext<QueueType>>() }
    {
        std::cout << "Constructing ThreadContextWrapper" << std::endl;
        Logger<QueueType>::get_instance().register_thread(_context);
    }

    ~ThreadContextWrapper()
    {
        if (_context)
        {
            _context->is_alive.store(false, std::memory_order_release);
        }
    }

    QueueType& get_queue()
    {
        return _context->q;
    }

  private:
    std::shared_ptr<ThreadContext<QueueType>> _context;
};

template <typename QueueType>
QueueType& get_thread_queue()
{
    static thread_local ThreadContextWrapper<QueueType> context_wrapper;
    return context_wrapper.get_queue();
}

template <typename QueueType, typename... Args>
void log(const char* fmt_str, Args&&... args)
{
    QueueType& q = get_thread_queue<QueueType>();

    size_t total_args_size
        = (Codec<std::remove_cvref_t<Args>>::encoded_size(args) + ...);
    size_t header_plus_args_size = total_args_size + sizeof(LogEventHeader);

    LogEventHeader header;
    header.fmt_str = fmt_str;
    header.payload_size = total_args_size;
    header.decoding_fn
        = &decode_and_format<QueueType, std::remove_cvref_t<Args>...>;

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
