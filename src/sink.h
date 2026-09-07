#ifndef _PAPPER_SINK_H_
#define _PAPPER_SINK_H_

#include <atomic>
#include <cstdio>
#include <string_view>

namespace papper::sink
{

class Sink
{
  public:
    ~Sink()
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

    // May ONLY be called by the backend thread
    // Note: Does not check for pending sink switch, that must be handled
    // by caller
    void write(const std::string_view& str)
    {
        fwrite(str.data(), 1, str.size(), _file);
        fputc('\n', _file);
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

            delete new_sink;
        }
    }

    // Can be called by any thread
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

  private:
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
    };

  private:
    FILE* _file = stdout;
    std::atomic<SinkDescriptor*> _pending_new_sink = nullptr;
};

}

#endif
