#include <cstring>
#include <iostream>
#include <thread>

#include "deserialize.h"
#include "spsc_queue.h"

// Should be aligned?
template <typename QueueType>
struct LogEventHeader
{
    const char* fmt_str = "";
    void (*decoding_fn)(const char*, QueueType&, std::string&);
};

template <typename QueueType, typename... Args>
void log(QueueType& queue, const char* fmt_str, Args&&... args)
{
    static_assert(
        (std::is_trivially_copyable_v<std::remove_reference_t<Args>> && ...),
        "arguments must be trivially copyable");

    constexpr size_t total_args_size = (sizeof(Args) + ...);
    constexpr size_t header_plus_args_size
        = total_args_size + sizeof(LogEventHeader<QueueType>);
    if (header_plus_args_size > queue.free_size())
        return; // Too big, drop this log event

    LogEventHeader<QueueType> header;
    header.fmt_str = fmt_str;
    header.decoding_fn = &decode_and_format<QueueType, Args...>;

    // Push header
    queue.push(reinterpret_cast<std::byte*>(&header),
               sizeof(LogEventHeader<QueueType>));
    // Push args
    ((queue.push(reinterpret_cast<std::byte*>(&args), sizeof(args))), ...);
}

int main()
{
    using QueueType = SPSCQueue<1000000>; // 1Mb
    QueueType queue;

    std::thread t_logger([&queue]() {
        while (true)
        {
            LogEventHeader<QueueType> event_header;
            if (queue.pop(reinterpret_cast<std::byte*>(&event_header),
                          sizeof(LogEventHeader<QueueType>)))
            {
                std::string output_str;
                event_header.decoding_fn(
                    event_header.fmt_str, queue, output_str);
                std::cout << output_str << std::endl;
            }
        }
    });

    std::thread t_producer([&queue]() {
        uint32_t i = 0;
        while (i++ < 10)
        {
            log(queue, "logging: {}, {:.5f}", i, (float)(i * 25.333));
            log(queue, "hej hopp {}", i - 1);
        }
    });

    t_logger.join();
    t_producer.join();

    return 0;
}
