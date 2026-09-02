#ifndef _DESERIALIZE_H_
#define _DESERIALIZE_H_

#include <cstring>
#include <string>
#include <tuple>

template <typename T, typename ArgsTuple>
void deserialize_non_trivially_copyable(ArgsTuple& tuple,
                                        const std::byte* buffer,
                                        size_t& offset)
{
    static_assert(false, "type not supported");
}

/*
 * std::string
 */
template <typename ArgsTuple>
void deserialize_non_trivially_copyable(ArgsTuple& tuple,
                                        const std::byte* buffer,
                                        size_t& offset)
{
    // deserialize string
}

template <typename... Args>
std::tuple<std::remove_reference_t<Args>...>
deserialize_to_tuple(const char* fmt_str, const std::byte* arg_data,
                     std::string& output)
{
    using ArgsTuple = std::tuple<std::remove_reference_t<Args>...>;
    ArgsTuple args;

    size_t offset = 0;

    auto deserialize_into_tuple = [&]<size_t I>() {
        if constexpr (std::is_trivially_copyable_v<
                          std::tuple_element_t<I, ArgsTuple>>)
        {
            std::memcpy(&std::get<I>(args),
                        arg_data + offset,
                        sizeof(std::tuple_element_t<I, ArgsTuple>));
            offset += sizeof(std::tuple_element_t<I, ArgsTuple>);
        }
        else
        {
            deserialize_non_trivially_copyable<
                std::tuple_element_t<I, ArgsTuple>,
                ArgsTuple>(args);
        }
    };

    [&]<size_t... Is>(std::index_sequence<Is...>) {
        (deserialize_into_tuple.template operator()<Is>(), ...);
    }(std::index_sequence_for<Args...>{});

    return args;

    // std::apply(
    //     [&](auto&&... v) {
    //         output = std::vformat(fmt_str, std::make_format_args(v...));
    //     },
    //     args);
}

#endif
