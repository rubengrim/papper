#include <cmath>
#include <string>
#include <thread>

#include "papper.h"

int main()
{
    log("{}", std::string("this is an example of a trace message in papper"));

    std::thread t1 = std::thread([]() {
        papper::init_thread("back");
        error("hej från här");
    });

    t1.join();

    return 0;
}
