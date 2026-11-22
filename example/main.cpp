#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <ratio>
#include <thread>
#include <unistd.h>
#include <vector>

#include "papper/papper.h"

int main()
{
    // PAPPER_SET_PATTERN("[{date:Ymd-} {time:HMSus}] {message} hello");

    PAPPER_TRACE("This is a long message: \n on multiple lines");
    PAPPER_DEBUG("msg");
    PAPPER_INFO("msg");
    PAPPER_WARN("msg");
    PAPPER_ERROR("msg");
}
