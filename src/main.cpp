#include <iostream>

#include "papper.h"

int main()
{
    uint32_t i = 0;
    while (i++ < 10000000)
    {
        papper::log("hej {}", std::to_string(i));
        // printf("hej %s\n", std::to_string(i).c_str());
    }

    return 0;
}
