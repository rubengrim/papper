#include <cstring>
#include <format>
#include <iostream>
#include <thread>
#include <tuple>
#include <utility>

#include "spsc_queue.h"

inline constexpr size_t arg_buf_size = 121;

struct alignas(128) LogEvent
{
    const char* fmt_str = "";
    std::byte arg_data[arg_buf_size] = {};
    void (*decoding_fn)(const char*, const std::byte*, std::string&);
};

template <typename QueueType, typename... Args>
void log(QueueType& queue, const char* fmt_str, Args&&... args)
{

    LogEvent event;
    event.fmt_str = fmt_str;
    event.decoding_fn = &deserialize_and_format<Args...>;

    size_t offset = 0;
    bool too_big = false;
    ((serialize_arg(event.arg_data, offset, args, too_big), ...));
    if (too_big) // Don't post the event if args don't fit in buffer
        return;

    queue.push(event);
}

int main()
{
    SPSCQueue<LogEvent, 500> queue;

    std::thread t_logger([&queue]() {
        while (true)
        {
            LogEvent event;
            if (queue.pop(event))
            {
                std::string output_str;
                event.decoding_fn(event.fmt_str, event.arg_data, output_str);
                std::cout << output_str << std::endl;
            }
        }
    });

    std::thread t_producer([&queue]() {
        uint32_t i = 0;
        while (i++ < 10)
        {
            log(queue, "logging: {}", i);
        }
    });

    t_logger.join();
    t_producer.join();

    return 0;
}
