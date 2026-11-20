#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#include "papper/papper.h"

int main()
{
    if (papper::pin_backend_thread(0))
    {
        INFO_B("pinning succeeded");
    }
    else
    {
        ERROR_B("pinning failed");
    }

    PAPPER_TRACE("msg");
    PAPPER_DEBUG("msg");
    PAPPER_INFO("msg");
    PAPPER_WARN("msg");
    PAPPER_ERROR("msg");
}
