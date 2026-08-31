#include <bit>
#include <cstring>
#include <iostream>
#include <print>
#include <thread>
#include <utility>

#include "spsc_queue.h"

inline constexpr size_t arg_buf_size = 121;

enum ArgType : uint8_t
{
    UINT32,
    FLOAT,
};

struct alignas(128) LogEvent
{
    const char* fmt_str = "";
    std::byte arg_data[arg_buf_size] = {};
};

template <typename T>
constexpr ArgType get_arg_type_info()
{
    // clang-format off
    if      constexpr (std::is_same_v<T, uint32_t>) return ArgType::UINT32;
    else if constexpr (std::is_same_v<T, float>)    return ArgType::FLOAT;
    else    static_assert(!sizeof(T), "unsupported argument type");
    // clang-format on
}

template <typename T>
void write_arg(std::byte* buffer, size_t& offset, const T& arg)
{
    ArgType type = get_arg_type_info<T>();
    if (offset + sizeof(ArgType) + sizeof(T) >= arg_buf_size)
        return; // Drop arg if it doesn't fit

    std::memcpy(buffer + offset, &type, sizeof(ArgType));
    offset += sizeof(ArgType);
    std::memcpy(buffer + offset, &arg, sizeof(T));
    offset += sizeof(T);
}

template <typename QueueType, typename... Args>
void log(QueueType& queue, const char* fmt, Args&&... args)
{
    LogEvent event;
    event.fmt_str = fmt;
    size_t offset = 0;
    ((write_arg(event.arg_data, offset, args), ...));
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
                size_t offset = 0;
                ArgType type;
                std::memcpy(&type, &event.arg_data + offset, sizeof(ArgType));
                offset += sizeof(ArgType);
                switch (type)
                {
                case ArgType::UINT32: {
                    uint32_t val;
                    std::memcpy(&val, &event.arg_data + offset, sizeof(uint32_t));
                    std::print("uint32_t: {}\n", val);
                    break;
                }
                case ArgType::FLOAT: {
                    float val;
                    std::memcpy(&val, &event.arg_data + offset, sizeof(float));
                    std::print("float: {}\n", val);
                    break;
                }
                }
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

    // std::cout << sizeof(LogEvent) << std::endl;

    return 0;
}
