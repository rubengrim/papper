#include <cstring>
#include <format>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "codec.h"
#include "queue.h"

// Should be aligned?
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

template <typename QueueType, typename... Args>
void log(QueueType& q, const char* fmt_str, Args&&... args)
{
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

int main()
{
    using QueueType = SPSCQueue<1000000>; // 1Mb
    QueueType q;

    std::thread t_logger([&q]() {
        const std::byte* buffer = nullptr;
        while (true)
        {
            buffer = q.reserve_read(sizeof(LogEventHeader));
            if (buffer != nullptr)
            {
                LogEventHeader header;
                Codec<LogEventHeader>::decode(buffer, header);
                q.commit_read();
                buffer = q.reserve_read(header.payload_size);

                std::string formatted_output;
                header.decoding_fn(header.fmt_str, buffer, formatted_output);
                q.commit_read();

                std::cout << formatted_output << std::endl;
            }
        }
    });

    std::thread t_producer([&q]() {
        uint32_t i = 0;
        while (i++ < 10000)
        {
            log(q, "hej {}", std::vector<int>{ 1, 2 });
        }
    });

    t_logger.join();
    t_producer.join();

    return 0;
}
