#ifndef _PAPPER_TIME_H
#define _PAPPER_TIME_H

// #include <stdio.h>
// #include <string.h>
// #include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#if (defined(__x86_64__) || defined(__i386__))
#include <x86intrin.h>
#endif

namespace papper::time
{

inline uint64_t read_tsc()
{
#if (defined(__aarch64__))

    uint64_t count;
    __asm__ volatile("mrs \t%0, cntvct_el0" : "=r"(count));
    return count;

#elif (defined(__x86_64__) || defined(__i386__))

    return __rdtsc();

#endif
}

#if (defined(__aarch64__))

// On ARM you can read cntfrq_el0 to get the tick freq (guaranteed to be
// invariant I think), and cntvct_el0 to get the tsc value

static bool get_tsc_ns_per_tick(double& tick_time)
{
    uint64_t freq;
    __asm__ volatile("mrs \t%0, cntfrq_el0" : "=r"(freq));
    tick_time = 1.0e9 / (double)freq;
    return true;
}

#elif (defined(__x86_64__) || defined(__i386__))

// On x86 it's more difficult than on ARM because not every cpu supports
// invariant tsc, and it's not standardized how you find out it it supports it
// or what the tsc tick frequency is

// This is used in cases where it was not possible to extract real info about
// tick freq, so we need to measure it
static double measure_tsc_tick()
{
    // TODO: This should be done multiple times and averaged, while catching
    // and discarding extreme values (due to rescheduling etc)
    auto start = std::chrono::steady_clock::now();
    uint64_t start_tick = read_tsc();
    auto end = start + std::chrono::milliseconds(10);

    while (std::chrono::steady_clock::now() < end)
    {
    }

    uint64_t elapsed = read_tsc() - start_tick;

    // ns per tick
    return 1.0e7 / (double)elapsed;
}

struct cpuid_t
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

static inline void x86_cpuid(int leaf, int subleaf, struct cpuid_t* p)
{
    __asm__ __volatile__(
        "cpuid"
        : "=a"(p->eax), "=b"(p->ebx), "=c"(p->ecx), "=d"(p->edx)
        : "a"(leaf), "c"(subleaf));
}

static std::string cpu_brand_name()
{
    cpuid_t cpuinfo;
    uint32_t int_buffer[4];
    char* buffer = (char*)&int_buffer[0];

    x86_cpuid(0x00000000, 0, &cpuinfo);
    // int* buffer_alias = (int*)&buffer[0];
    int_buffer[0] = cpuinfo.ebx;
    int_buffer[1] = cpuinfo.edx;
    int_buffer[2] = cpuinfo.ecx;
    buffer[12] = char(0);

    return std::string(buffer);
}

static bool on_intel()
{
    // N.B. Apple Rosetta 2 also claims to be GenuineIntel...
    return cpu_brand_name() == "GenuineIntel";
}

static bool on_amd()
{
    return cpu_brand_name() == "AuthenticAMD";
}

static std::string cpu_model_name();

// Despite Wikipedia's assertion that this is what Rosetta returns,
// what I see is "GenuineIntel", which seems rather dubious IMO.
static bool on_apple_rosetta()
{
    // This should work, whereas the straighforward brand name check below does
    // not.
    return cpu_model_name().find("Apple") != std::string::npos;
    // return CPUBrandName() == "VirtualApple";
}

// This does more than we need simply to extract the invariant TSC
// rate, but is useful anyway. We can get the TSC rate from here on Intel
// if we can't find it anywhere else.
static std::string cpu_model_name()
{
    cpuid_t cpuinfo;
    char brand[256];
    memset(&brand[0], 0, sizeof(brand));

    auto ids = 0;
    if (on_amd())
    {
        // AMD always support three leaves here.
        ids = 3;
    }
    else if (on_intel())
    {
        x86_cpuid(0x80000000, 0, &cpuinfo);
        // On Intel this gives the number of extra fields to read
        ids = cpuinfo.eax ^ 0x80000000;
    }
    else
    {
        std::string brandName = cpu_brand_name();
        // errPrintf("Unknown CPU vendor; not sure how to read the
        // CPUModelName. "
        //           "Brand: '%s'\n",
        //           brandName.c_str());
        return brandName;
    }

    for (int i = 0; i < ids; i++)
        x86_cpuid(i + 0x80000002, 0, (cpuid_t*)(brand + i * sizeof(cpuid_t)));
    // Remove trailing blanks.
    char* start = &brand[0];
    for (char* end = &start[strlen(start) - 1]; end > start && *end == ' ';
         end--)
    {
        *end = char(0);
    }

    // Remove leading blanks
    for (; *start == ' '; start++)
        ;

    // errPrintf("CPU model name from cpuid: '%s'\n", start);
    return std::string(start);
}

// Try to extract tick time from cpuid information
// This does not work on AMD!
static bool extract_tick_time_from_leaf_15h(double* time)
{
    // From Intel PRM:
    // Intel Cpuid leaf  15H
    // If EBX[31:0] is 0, the TSC/”core crystal clock” ratio is not enumerated.
    // EBX[31:0]/EAX[31:0] indicates the ratio of the TSC frequency and the
    // core crystal clock frequency. If ECX is 0, the nominal core crystal
    // clock frequency is not enumerated. “TSC frequency” = “core crystal clock
    // frequency” * EBX/EAX. The core crystal clock may differ from the
    // reference clock, bus clock, or core clock frequencies. EAX Bits 31 - 00:
    // An unsigned integer which is the denominator of the TSC/”core crystal
    // clock” ratio. EBX Bits 31 - 00: An unsigned integer which is the
    // numerator of the TSC/”core crystal clock” ratio. ECX Bits 31 - 00: An
    // unsigned integer which is the nominal frequency of the core crystal
    // clock in Hz. EDX Bits 31 - 00: Reserved = 0.
    cpuid_t cpuinfo;

    x86_cpuid(0x0, 0, &cpuinfo);
    if (cpuinfo.eax < 0x15)
    {
        // Leaf doesn't exist
        return false;
    }

    x86_cpuid(0x15, 0, &cpuinfo);
    if (cpuinfo.ebx == 0 || cpuinfo.ecx == 0)
    {
        // cpuid node 15h exists but does not give frequency
        return false;
    }
    double core_crystal_freq = cpuinfo.ecx;
    *time = (1.0e9 * cpuinfo.eax) / (cpuinfo.ebx * core_crystal_freq);

    return true;
}

// Try to extract tick time from the brand string
// Only works on Intel
static bool extract_tick_time_from_name(double* time)
{
    auto brand_string = cpu_model_name();
    char const* brand = brand_string.c_str();
    auto end = brand + strlen(brand) - 3;
    uint64_t multiplier;

    if (*end == 'M')
        multiplier = 1000LL * 1000LL;
    else if (*end == 'G')
        multiplier = 1000LL * 1000LL * 1000LL;
    else if (*end == 'T')
        multiplier = 1000LL * 1000LL * 1000LL * 1000LL;
    else
    {
        std::cout << "FALSE" << std::endl;
        return false;
    }
    while (*end != ' ' && end >= brand)
        end--;
    char* uninteresting;
    double freq = strtod(end + 1, &uninteresting);
    if (freq == 0.0)
    {
        std::cout << "frequency is 0" << std::endl;
        return false;
    }

    *time = 1.0e9 / (freq * (double)multiplier);

    return true;
}

static bool get_tsc_ns_per_tick(double& tick_time)
{
    // First check whether TSC can sanely be used at all.
    // These leaves are common to Intel and AMD.
    cpuid_t cpuinfo;
    // Does the leaf that can tell us that exist?
    x86_cpuid(0x80000000, 0, &cpuinfo);
    if (cpuinfo.eax < 0x80000007)
    {
        // This processor cannot tell us whether it has invariant tsc
        std::cout << "cannot give invariant tsc status" << std::endl;
        return false;
    }
    // At least the CPU can tell us whether it supports an invariant TSC.
    x86_cpuid(0x80000007, 0, &cpuinfo);
    if ((cpuinfo.edx & (1 << 8)) == 0)
    {
        // This processor does not have invariant tsc
        std::cout << "does not have invariant tsc" << std::endl;
        return false;
    }

    double res;
    // Try to get it from Intel's leaf15H
    if (extract_tick_time_from_leaf_15h(&res))
    {
        std::cout << "extracted from leaf" << std::endl;
        tick_time = res;
        return true;
    }

    if (on_intel() && !on_apple_rosetta())
    {
        // Try to get it from the brand name of the CPU (Intel have it there as
        // a string), and do seem to have the TSC clock run at the notional CPU
        // frequency.
        if (extract_tick_time_from_name(&res))
        {
            std::cout << "extracted from name" << std::endl;
            tick_time = res;
            return true;
        }
    }

    // There was no good way of finding the tick time, so measure it
    tick_time = measure_tsc_tick();
    return true;
}
#endif

// This is not thread safe, is only meant to be used by backend
class TimestampConverter
{
  public:
    TimestampConverter() {}

    bool init()
    {
        if (!get_tsc_ns_per_tick(_ns_per_tick))
            return false; // cpu does not have invariant tsc

        sync_to_system_clock();
        return true;
    }

    void sync_to_system_clock()
    {
        uint64_t tsc1;
        uint64_t tsc2;
        std::chrono::system_clock::time_point t;

        tsc1 = read_tsc();
        t = std::chrono::system_clock::now();
        tsc2 = read_tsc();

        // Average to hopefully align better with the exact time read
        uint64_t avg = (uint64_t)((double)tsc1 + (double)(tsc2 - tsc1) / 2.0);

        _sync_point = SyncPoint{ .ts = avg, .system_time = t };
    }

    // Convert timestamp (i.e. ticks since system startup) to wall
    // clock time
    std::chrono::system_clock::time_point
    timestamp_to_system_time(const uint64_t ts)
    {
        double ns_since_sync = _ns_per_tick * (double)((int64_t)(ts - _sync_point.ts));
        return std::chrono::time_point_cast<std::chrono::nanoseconds>(
                   _sync_point.system_time)
               + std::chrono::nanoseconds((int64_t)ns_since_sync);
    }

  private:
    // Stores the system time together with the value of the tsc at that same
    // time point, so that another timestamp can be converted to actual wall
    // clock time
    struct SyncPoint
    {
        uint64_t ts;
        std::chrono::system_clock::time_point system_time;
    };

    SyncPoint _sync_point;
    double _ns_per_tick = 0;
};

}

#endif
