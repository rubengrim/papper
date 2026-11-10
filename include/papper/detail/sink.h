#pragma once

#include "papper/detail/pattern.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace papper::detail
{

class SinkHandler
{
  public:
    ~SinkHandler()
    {
        SinkDescriptor* pending
            = _pending_new_sink.exchange(nullptr, std::memory_order_acq_rel);
        if (pending != nullptr)
        {
            close_file(pending->file);
            delete pending;
        }
        close_file(_sink);
    }

    void format_and_write(const MessageData& data)
    {
        std::string formatted_message = _pattern_formatter.format(
            data, _colors_enabled && _sink_supports_colors);
        fwrite(formatted_message.data(), 1, formatted_message.size(), _sink);
        fputc('\n', _sink);
    }

    void queue_new_sink(FILE* file, const int buffering_mode,
                        const size_t buffer_size)
    {
        if (file == nullptr)
            return;

        SinkDescriptor* new_sink
            = new SinkDescriptor{ file, buffering_mode, buffer_size };

        SinkDescriptor* prev
            = _pending_new_sink.exchange(new_sink, std::memory_order_acq_rel);
        if (prev != nullptr)
        {
            close_file(prev->file);
            delete prev;
        }
    }

    void queue_new_pattern_formatter(PatternFormatterBase* formatter)
    {
        _pattern_formatter.queue_new_pattern_formatter(formatter);
    }

    void set_colors_enabled(bool enabled)
    {
        _colors_enabled = enabled;
    }

    void queue_new_colors(const ColorConfig& colors)
    {
        _pattern_formatter.queue_new_colors(colors);
    }

    void poll_for_pending_sink_switch()
    {
        SinkDescriptor* new_sink
            = _pending_new_sink.exchange(nullptr, std::memory_order_acq_rel);
        if (new_sink != nullptr)
        {
            fflush(_sink);
            close_file(_sink);
            _sink = new_sink->file;
            // TODO: setvbuf could fail, so might want to catch that somehow
            setvbuf(_sink,
                    nullptr,
                    new_sink->buffering_mode,
                    new_sink->buffer_size);

            _sink_supports_colors = get_sink_color_support();

            delete new_sink;
        }
    }

    void poll_for_pending_formatter_switch()
    {
        _pattern_formatter.poll_for_pending_formatter_switch();
    }

    void poll_for_pending_color_switch()
    {
        _pattern_formatter.poll_for_pending_color_switch();
    }

  private:
    void close_file(FILE* file)
    {
        if (file != stdout && file != stderr)
        {
            fclose(file);
        }
    }

    bool get_sink_color_support() const
    {
#if defined(__unix__) || defined(__APPLE__)
        if (_sink == nullptr)
            return false;

        // Check if sink is regular file
        const int fd = fileno(_sink);
        if (fd < 0 || !isatty(fd))
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
        // No windows support for now I don't know how this works there
        return false;
#endif
    }

  private:
    struct SinkDescriptor
    {
        FILE* file = stdout;
        int buffering_mode = _IOLBF;
        size_t buffer_size = BUFSIZ;
    };

  private:
    FILE* _sink = stdout;
    std::atomic<SinkDescriptor*> _pending_new_sink = nullptr;
    PatternFormatterHandler _pattern_formatter;
    bool _colors_enabled = true; // If true, use colors when sink supports it
    bool _sink_supports_colors = get_sink_color_support();
};

}
