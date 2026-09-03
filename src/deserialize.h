#ifndef _DESERIALIZE_H_
#define _DESERIALIZE_H_

#include <cstring>
#include <format>
#include <string>
#include <tuple>

// template <typename T, typename ArgsTuple>
// void deserialize_non_trivially_copyable(ArgsTuple& tuple,
//                                         const std::byte* buffer,
//                                         size_t& offset)
// {
//     static_assert(false, "type not supported");
// }

// /*
//  * std::string
//  */
// template <typename ArgsTuple>
// void deserialize_non_trivially_copyable(ArgsTuple& tuple,
//                                         const std::byte* buffer,
//                                         size_t& offset)
// {
//     // deserialize string
// }

template <typename QueueType, typename... Args>
std::tuple<std::remove_reference_t<Args>...>
deserialize_to_tuple(QueueType& queue)
{
    using ArgsTuple = std::tuple<std::remove_reference_t<Args>...>;
    ArgsTuple args;

    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (queue.pop(reinterpret_cast<std::byte*>(&std::get<Is>(args)),
                   sizeof(std::tuple_element_t<Is, ArgsTuple>)),
         ...);
    }(std::index_sequence_for<Args...>{});

    return args;
}

template <typename QueueType, typename... Args>
void decode_and_format(const char* fmt_str, QueueType& queue,
                       std::string& formatted_output)
{
    auto args_tuple = deserialize_to_tuple<QueueType, Args...>(queue);

    std::apply(
        [&](auto&&... v) {
            formatted_output
                = std::vformat(fmt_str, std::make_format_args(v...));
        },
        args_tuple);
}

#endif
