#pragma once

#include <cstdio>

#include "papper/detail/color.h"
#include "papper/detail/pattern.h"

namespace papper::detail
{

// Control events are sent through the thread queues like normal log events,
// then the backend reads them and performs a change to the logger config based
// on the event For example, a NewSink control event tells the backend to
// change to a new sink
enum ControlEventType
{
    NewSink,
    NewPattern,
    NewColors,
    ColorEnable,
};

struct NewSinkControlEvent
{
    FILE* file;
    int buffering_mode;
    size_t buffer_size;
};

struct NewPatternControlEvent
{
    const PatternFormatterBase* formatter;
};

struct NewColorsControlEvent
{
    const ColorConfig* colors;
};

struct ColorEnableControlEvent
{
    bool enabled;
};

union ControlEvent
{
    NewSinkControlEvent new_sink;
    NewPatternControlEvent new_pattern;
    NewColorsControlEvent new_colors;
    ColorEnableControlEvent colors_enabled;
};

}
