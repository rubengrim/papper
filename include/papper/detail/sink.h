#pragma once

#include "papper/detail/color.h"
#include "papper/detail/pattern.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

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
        fflush(_sink);
        close_file(_sink);
    }

    void format_and_write(const MessageData& data)
    {
        std::string formatted_message = _pattern_formatter.format(
            data, _colors_enabled && _sink_supports_colors);
        fwrite(formatted_message.data(), 1, formatted_message.size(), _sink);
        fputc('\n', _sink);
    }

    void set_new_sink(FILE* file, const int buffering_mode,
                      const size_t buffer_size)
    {
        if (file == nullptr)
            return;

        fflush(_sink);
        close_file(_sink);

        _sink = file;

        // TODO: setvbuf could fail, so might want to catch that somehow
        setvbuf(_sink, nullptr, buffering_mode, buffer_size);

        _sink_supports_colors = get_sink_color_support();
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

    void flush()
    {
        fflush(_sink);
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
        // No windows support for now I can't test on there
        return false;
#endif
    }

  private:
    FILE* _sink = stdout;
    bool _sink_supports_colors = get_sink_color_support();
    bool _colors_enabled = true; // If true, use colors if sink supports it
    PatternFormatterHandler _pattern_formatter;
};

}
