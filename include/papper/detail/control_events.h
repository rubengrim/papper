#pragma once

#include <cstdio>
#include <cstdlib>

#include "papper/detail/color.h"
#include "papper/detail/pattern.h"
#include "papper/detail/sink.h"

namespace papper::detail
{

// Control events are sent through the thread queues like normal log events,
// then the backend reads them and performs a change to the logger config based
// on the event For example, a NewSink control event tells the backend to
// change to a new sink
enum class ControlEventType
{
    Sink,
    FlushBehaviour,
    Pattern,
    Colors,
    ColorsEnable,
    CPUAffinity,
    BackendSleepDuration,
};

enum ThreadControlEventStatus
{
    NotQueued,
    InQueue,
    Succeeded,
    Failed,
};

struct SinkEvent
{
    FILE* file;
};

struct FlushBehaviourEvent
{
    FlushMode mode;
    size_t buffer_size;
    size_t max_flush_interval_ms;
};

struct PatternEvent
{
    const PatternFormatterBase* formatter;
};

struct ColorsEvent
{
    const ColorConfig* colors;
};

struct ColorEnabledEvent
{
    bool enabled;
};

struct CPUAffinityEvent
{
    int cpu_id;
};

struct BackendSleepDurationEvent
{
    std::chrono::system_clock::duration duration;
};

union ControlEvent
{
    SinkEvent sink;
    FlushBehaviourEvent flush_behaviour;
    PatternEvent pattern;
    ColorsEvent colors;
    ColorEnabledEvent colors_enabled;
    CPUAffinityEvent cpu_affinity;
    BackendSleepDurationEvent sleep_duration;
};

}
