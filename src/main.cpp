#include <iostream>

#include "core.h"

int main()
{
    // using QueueType = SPSCQueue<1000000>; // 1Mb
    // QueueType q;

    // std::thread t_logger([&q]() {
    //     const std::byte* buffer = nullptr;
    //     while (true)
    //     {
    //         buffer = q.reserve_read(sizeof(LogEventHeader));
    //         if (buffer != nullptr)
    //         {
    //             LogEventHeader header;
    //             Codec<LogEventHeader>::decode(buffer, header);
    //             q.commit_read();
    //             buffer = q.reserve_read(header.payload_size);

    //             std::string formatted_output;
    //             header.decoding_fn(header.fmt_str, buffer,
    //             formatted_output); q.commit_read();

    //             std::cout << formatted_output << std::endl;
    //         }
    //     }
    // });

    // std::thread t_producer([&q]() {
    //     uint32_t i = 0;
    //     while (i++ < 10000)
    //     {
    //         log(q, "hej {}", std::to_string(i));
    //     }
    // });

    // t_logger.join();
    // t_producer.join();

    uint32_t i = 0;
    while (i++ < 100)
    {
        log<SPSCQueue<100000>>("hej {}", std::to_string(i));
    }

    // std::string a;
    // std::getline(std::cin, a);

    return 0;
}
