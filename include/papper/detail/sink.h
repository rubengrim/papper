#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string_view>

#if defined(_WIN32)
#include <io.h>

// Declared manually instead of including windows.h, so that papper doesn't
// leak its macros (min, max, ERROR etc) into user code. The signatures match
// the ones in windows.h (BOOL = int, HANDLE = void*, DWORD = unsigned long), so
// they don't conflict if the user includes windows.h as well
extern "C" __declspec(dllimport) int __stdcall GetConsoleMode(void*,
                                                             unsigned long*);
extern "C" __declspec(dllimport) int __stdcall SetConsoleMode(void*,
                                                             unsigned long);
#else
#include <unistd.h>
#endif

#include "color.h"

namespace papper::detail
{

#if defined(_WIN32)
namespace win32
{
// Same values as in windows.h
inline void* const invalid_handle_value = (void*)(intptr_t)-1;
constexpr unsigned long enable_virtual_terminal_processing = 0x0004;
}
#endif

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
        close_file(_file);
    }

    // Can be called by any thread
    void queue_new_sink(FILE* file, const int buffering_mode,
                        const size_t buffer_size, const ColorMode color_mode)
    {
        if (file == nullptr)
            return;

        SinkDescriptor* new_sink = new SinkDescriptor{
            file, buffering_mode, buffer_size, color_mode
        };

        SinkDescriptor* prev
            = _pending_new_sink.exchange(new_sink, std::memory_order_acq_rel);
        if (prev != nullptr)
        {
            close_file(prev->file);
            delete prev;
        }
    }

    // May ONLY be called by the backend thread
    // Note: Does not check for pending sink switch, that must be handled
    // by caller
    void write(std::string_view str)
    {
        fwrite(str.data(), 1, str.size(), _file);
    }

    // May ONLY be called by the backend thread
    // Note: Does not check for pending sink switch, that must be handled
    // by caller
    void write_str_and_endl(std::string_view str)
    {
        fwrite(str.data(), 1, str.size(), _file);
        fputc('\n', _file);
#if defined(_WIN32)
        // The MS CRT treats _IOLBF as _IOFBF, so line buffering has to be done
        // manually
        if (_buffering_mode == _IOLBF)
            fflush(_file);
#endif
    }

    // May ONLY be called by the backend thread
    bool colored_output() const
    {
        return _colored_output;
    }

    // May ONLY be called by the backend thread
    void poll_for_pending_sink_switch()
    {
        SinkDescriptor* new_sink
            = _pending_new_sink.exchange(nullptr, std::memory_order_acq_rel);
        if (new_sink != nullptr)
        {
            fflush(_file);
            close_file(_file);
            _file = new_sink->file;
            // TODO: setvbuf could fail, so might want to catch that somehow
            setvbuf(_file,
                    nullptr,
                    new_sink->buffering_mode,
                    new_sink->buffer_size);
            _buffering_mode = new_sink->buffering_mode;
            _colored_output
                = resolve_color_mode(_file, new_sink->color_mode);

            delete new_sink;
        }
    }

  private:
    // Returns true if file is a terminal that will interpret ANSI codes
    static bool is_ansi_terminal(FILE* file)
    {
#if defined(_WIN32)
        // _isatty is true for any character device (NUL etc), so check for an
        // actual console instead
        void* handle = (void*)_get_osfhandle(_fileno(file));
        unsigned long mode = 0;
        if (handle == win32::invalid_handle_value
            || !GetConsoleMode(handle, &mode))
            return false;

        // The console has to be told to interpret ANSI codes. Fails before
        // Windows 10, in which case we can't color
        return (mode & win32::enable_virtual_terminal_processing)
               || SetConsoleMode(
                   handle, mode | win32::enable_virtual_terminal_processing);
#else
        return isatty(fileno(file)) == 1;
#endif
    }

    static bool resolve_color_mode(FILE* file, const ColorMode color_mode)
    {
        if (color_mode == ColorMode::Never)
            return false;

        // Always call this, since on Windows it also enables ANSI codes in the
        // console for ColorMode::Always
        const bool ansi_terminal = is_ansi_terminal(file);
        return color_mode == ColorMode::Always || ansi_terminal;
    }

    void close_file(FILE* file)
    {
        if (file != stdout && file != stderr)
        {
            fclose(file);
        }
    }

    struct SinkDescriptor
    {
        FILE* file = stdout;
        int buffering_mode = _IOLBF;
        size_t buffer_size = BUFSIZ;
        ColorMode color_mode = ColorMode::Auto;
    };

  private:
    FILE* _file = stdout;
    int _buffering_mode = _IOLBF;
    bool _colored_output = resolve_color_mode(stdout, ColorMode::Auto);
    std::atomic<SinkDescriptor*> _pending_new_sink = nullptr;
};

}
