#include "papper/detail/core.h"
#include "papper/detail/level.h"
#include "papper/detail/time.h"
#include "papper/papper.h"
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <unistd.h>

papper::detail::Clock c;
double begin = 0;
size_t n_warmup = 10'000;
size_t n_messages = 10'000'000;

// Optional perf control. Run from the directory containing the fifos:
//   mkfifo ctl.fifo ack.fifo
//   perf record -D -1 --control fifo:ctl.fifo,ack.fifo ... ./ex
// Without perf running, the fifos have no reader and perf control is skipped.
int perf_ctl_fd = -1;
int perf_ack_fd = -1;

void perf_open()
{
    // O_NONBLOCK makes this fail with ENXIO instead of blocking when perf
    // isn't holding the fifo open
    perf_ctl_fd = open("ctl.fifo", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (perf_ctl_fd < 0)
        return;
    perf_ack_fd = open("ack.fifo", O_RDONLY | O_CLOEXEC);
    if (perf_ack_fd < 0)
    {
        close(perf_ctl_fd);
        perf_ctl_fd = -1;
        return;
    }
    fcntl(perf_ctl_fd, F_SETFL, 0); // back to blocking writes
}

void perf_command(const char* cmd, size_t len)
{
    if (perf_ctl_fd < 0 || perf_ack_fd < 0)
        return;
    if (write(perf_ctl_fd, cmd, len) != (ssize_t)len)
        return;
    char buf[8];
    (void)read(perf_ack_fd, buf, sizeof(buf)); // wait for "ack\n"
}

void finish()
{
    // perf_command("disable\n", 8);
    double elapsed_seconds = (c.read_timestamp_ns() - begin) * 1e-9;
    double msgs_per_second = n_messages / elapsed_seconds / 1e6;
    std::cout << msgs_per_second << " million msgs/s" << std::endl;
}

int main()
{
    // std::atexit(finish);
    // perf_open();

    // PAPPER_SET_PATTERN("{time:HMSF} {message}");

    // if (papper::set_sink(
    //         "test.log", true, papper::FlushOn::BufferFull, 64 * 1024)
    //     != 0)
    //     std::cout << "Fail" << std::endl;

    // papper::set_default_queue_size((n_messages + n_warmup) * 128);
    // papper::init_thread();

    // c.init();

    // // Warmup
    // for (size_t i = 0; i < n_warmup; i++)
    // {
    //     TRACE("message {}", i);
    // }

    // std::string s = "hej";
    // perf_command("enable\n", 7);
    // begin = c.read_timestamp_ns();
    // for (size_t i = 0; i < n_messages; i++)
    // {
    //     PAPPER_INFO("Logging int: {}, double: {:.3f}, string: {}", i, 0.4,
    //     s);
    // }
    // perf_command("disable\n", 8);

    PAPPER_SET_PATTERN("{time:HMSF} {message}");
    for (int i = 0; i < 10; i++)
    {
        PAPPER_TRACE("hej {}, {}", i, "hopp");
    }
}
