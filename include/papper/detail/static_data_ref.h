#pragma once

#include "codec.h"

namespace papper::detail
{

// Data with a static lifetime can be wrapped by a StaticDataRef to allow it to
// be encoded into the queue
template <typename T>
class StaticDataRef
{
  public:
    explicit StaticDataRef(T* data) : data_ptr{ data } {}

    T* get() const
    {
        return data_ptr;
    }

  private:
    T* data_ptr;
};

template <typename T>
struct Codec<StaticDataRef<T>>
{
    // Always decoded into a pointer to the type the reference holds
    using DecodedT = T*;

    static size_t encoded_size(const StaticDataRef<T>&)
    {
        return sizeof(T*);
    }

    static void encode(std::byte*& buffer, const StaticDataRef<T>& value,
                       const size_t)
    {
        T* ptr = value.get();
        std::memcpy(buffer, &ptr, sizeof(T*));
        buffer += sizeof(T*);
    }

    static void decode(const std::byte*& buffer, DecodedT& value)
    {
        std::memcpy(&value, buffer, sizeof(T*));
        buffer += sizeof(T*);
    }
};

}
