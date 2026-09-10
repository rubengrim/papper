#ifndef _PAPPER_LEVEL_H_
#define _PAPPER_LEVEL_H_

#include <cstdint>

namespace papper
{

enum Level : uint8_t
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
};

}

#endif
