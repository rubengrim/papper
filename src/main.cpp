#include <bit>
#include <cstring>
#include <format>
#include <iostream>
#include <print>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "spsc_queue.h"

inline constexpr size_t arg_buf_size = 121;

struct alignas(128) LogEvent
{
    const char* fmt_str = "";
    std::byte arg_data[arg_buf_size] = {};
    void (*decoding_fn)(const char*, const std::byte*, std::string&);
};

template <typename... Args>
void deserialize_and_format(const char* fmt_str, const std::byte* arg_data,
                            std::string& output)
{
    using ArgsTuple = std::tuple<std::remove_reference_t<Args>...>;
    ArgsTuple args;

    size_t offset = 0;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        ((std::memcpy(&std::get<Is>(args),
                      arg_data + offset,
                      sizeof(std::tuple_element_t<Is, ArgsTuple>)),
          offset += sizeof(std::tuple_element_t<Is, ArgsTuple>)),
         ...);
    }(std::index_sequence_for<Args...>{});

    std::apply(
        [&](auto&&... v) {
            output = std::vformat(fmt_str, std::make_format_args(v...));
        },
        args);
}

template <typename T>
void write_arg(std::byte* buffer, size_t& offset, const T& arg, bool& too_big)
{
    if (offset + sizeof(T) >= arg_buf_size)
    {
        too_big = true;
        return;
    }

    std::memcpy(buffer + offset, &arg, sizeof(T));
    offset += sizeof(T);
}

template <typename QueueType, typename... Args>
void log(QueueType& queue, const char* fmt_str, Args&&... args)
{
    static_assert(
        (std::is_trivially_copyable_v<std::remove_reference_t<Args>> && ...),
        "format arguments must be trivially copyable");

    LogEvent event;
    event.fmt_str = fmt_str;
    event.decoding_fn = &deserialize_and_format<Args...>;

    size_t offset = 0;
    bool too_big = false;
    ((write_arg(event.arg_data, offset, args, too_big), ...));
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
        while (i++ < 10000)
        {
            log(queue, "logging: {}", i);
        }
    });

    t_logger.join();
    t_producer.join();

    return 0;
}
