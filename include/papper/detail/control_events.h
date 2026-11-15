#pragma once

#include <cstdio>

#include "papper/detail/color.h"
#include "papper/detail/pattern.h"
#include "papper/detail/sink.h"

namespace papper::detail
{

// Control events are sent through the thread queues like normal log events,
// then the backend reads them and performs a change to the logger config based
// on the event For example, a NewSink control event tells the backend to
// change to a new sink
enum ControlEventType
{
    NewSink,
    NewFlushConfig,
    NewPattern,
    NewColors,
    ColorEnable,
};

struct NewSinkEvent
{
    FILE* file;
};

struct NewFlushBehaviourEvent
{
    FlushMode mode;
    size_t buffer_size;
    size_t max_flush_interval_ms;
};

struct NewPatternEvent
{
    const PatternFormatterBase* formatter;
};

struct NewColorsEvent
{
    const ColorConfig* colors;
};

struct ColorEnabledEvent
{
    bool enabled;
};

union ControlEvent
{
    NewSinkEvent new_sink;
    NewFlushBehaviourEvent new_flush_behaviour;
    NewPatternEvent new_pattern;
    NewColorsEvent new_colors;
    ColorEnabledEvent colors_enabled;
};

}
