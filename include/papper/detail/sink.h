#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "color.h"
#include "globals.h"
#include "pattern.h"
#include "platform.h"
#include "time.h"

#if defined(PAPPER_LINUX) || defined(PAPPER_UNIX) || defined(PAPPER_APPLE)
#include <unistd.h>
#endif

namespace papper
{

enum class FlushMode
{
    Auto, // Will use LineEnd for terminals and BufferFull for files
    LineEnd,
    BufferFull,
};

struct FlushConfig
{
    FlushMode flush_mode = FlushMode::Auto;
    size_t buffer_size = detail::globals::default_flush_buffer_size;
    size_t max_flush_interval_ms
        = detail::globals::default_max_flush_interval_ms;
};

}

namespace papper::detail
{

class SinkHandler
{
  public:
    SinkHandler()
    {
        _pattern_formatter = PatternFormatterHandler{};

        _sink = stdout;
        _sink_is_terminal = sink_is_terminal();
        _sink_supports_colors = get_sink_color_support();
        _colors_enabled = true;

        _flush_mode = FlushMode::Auto;
        _buffered_flushing = resolve_flush_mode();
        _flush_buffer_size = globals::default_flush_buffer_size;
        _max_flush_interval_ns
            = (double)(globals::default_max_flush_interval_ms * 1e6);
        _last_flush_time = (int64_t)read_tsc();
    }

    ~SinkHandler()
    {
        flush();
        close_file(_sink);
    }

    void format_and_buffer(const MessageData& data)
    {
        _pattern_formatter.format(
            data, _colors_enabled && _sink_supports_colors, _buffer);
        _buffer.push_back('\n');

        if (!_buffered_flushing || _buffer.size() > _flush_buffer_size)
        {
            flush();
            _last_flush_time = read_tsc();
        }
    }

    void poll_for_flush_timeout(const Clock& clock)
    {
        if (_buffered_flushing)
        {
            int64_t now = (int64_t)read_tsc();
            if (clock.to_ns(now - _last_flush_time) >= _max_flush_interval_ns)
            {
                flush();
                _last_flush_time = now;
            }
        }
    }

    void flush()
    {
        fwrite(_buffer.data(), 1, _buffer.size(), _sink);
        fflush(_sink);
        _buffer.clear();
    }

    void set_new_sink(FILE* file)
    {
        if (file == nullptr)
            return;

        flush();
        close_file(_sink);

        _sink = file;
        // Set to unbuffered mode since papper handles buffering
        setvbuf(_sink, nullptr, _IONBF, 0);

        _sink_is_terminal = sink_is_terminal();
        _sink_supports_colors = get_sink_color_support();
        _buffered_flushing = resolve_flush_mode();
    }

    void set_flush_behaviour(FlushMode mode, size_t buffer_size,
                             size_t max_flush_interval_ms)
    {
        _flush_mode = mode;
        _buffered_flushing = resolve_flush_mode();

        _max_flush_interval_ns = (double)(max_flush_interval_ms * 1e6);
        _flush_buffer_size = buffer_size;
        if (_flush_buffer_size > _buffer.capacity())
        {
            // The buffer is flushed when it's size exceeds
            // _flush_buffer_size, so allocate 50% more than needed for
            // some breathing room
            _buffer.reserve(_flush_buffer_size * 1.5);
        }
    }

    void set_new_pattern_formatter(
        std::unique_ptr<const PatternFormatterBase> formatter)
    {
        _pattern_formatter.set_new_pattern_formatter(std::move(formatter));
    }

    void set_new_colors(std::unique_ptr<const ColorConfig> colors)
    {
        _pattern_formatter.set_new_colors(std::move(colors));
    }

    void set_colors_enabled(bool enabled)
    {
        _colors_enabled = enabled;
    }

  private:
    void close_file(FILE* file)
    {
        if (file != stdout && file != stderr)
        {
            fclose(file);
        }
    }

    // Returns whether or not the sink should use buffered flushing based on
    // the flush mode If mode is auto, use buffered flushing only for files
    bool resolve_flush_mode() const
    {
        if (_flush_mode == FlushMode::Auto)
        {
            return !_sink_is_terminal;
        }
        else
        {
            return _flush_mode == FlushMode::BufferFull;
        }
    }

    bool sink_is_terminal() const
    {
#if defined(PAPPER_LINUX) || defined(PAPPER_UNIX) || defined(PAPPER_APPLE)
        const int fd = fileno(_sink);
        return fd >= 0 && isatty(fd) != 0;
#else
        // No windows support for now I can't test on there, assume it's a
        // regular file
        return false;
#endif
    }

    bool get_sink_color_support() const
    {
#if defined(PAPPER_LINUX) || defined(PAPPER_UNIX) || defined(PAPPER_APPLE)
        if (_sink == nullptr)
            return false;

        // Check if sink is regular file
        if (!sink_is_terminal())
        {
            return false;
        }

        // Check if sink is terminal but still doesn't support color codes
        const char* term = std::getenv("TERM");
        if (term == nullptr || term[0] == '\0'
            || std::strcmp(term, "dumb") == 0)
        {
            return false;
        }

        // no-color.org
        const char* no_color = std::getenv("NO_COLOR");
        if (no_color != nullptr && no_color[0] != '\0')
        {
            return false;
        }

        return true;
#else
        // No windows support for now I can't test on there
        return false;
#endif
    }

  private:
    PatternFormatterHandler _pattern_formatter;

    FILE* _sink;
    bool _sink_is_terminal;
    bool _sink_supports_colors;
    bool _colors_enabled;

    Buffer _buffer;
    FlushMode _flush_mode;
    bool _buffered_flushing;
    size_t _flush_buffer_size;
    double _max_flush_interval_ns;
    int64_t _last_flush_time;
};

}
