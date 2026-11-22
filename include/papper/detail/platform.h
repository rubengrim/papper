#pragma once

#if defined(__x86_64__)
#define PAPPER_X86
#elif defined(__aarch64__)
#define PAPPER_ARM
#else
#error This CPU architecture is not supported by papper
#endif

#if defined(__APPLE__)
#define PAPPER_APPLE
#elif defined(__linux__)
#define PAPPER_LINUX
#elif defined(__unix__)
#define PAPPER_UNIX
#elif defined(_WIN64) || defined(_WIN32)
#error Windows is not supported by papper yet
#else
#error This platform is not supported by papper
#endif
