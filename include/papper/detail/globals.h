#pragma once

#include <atomic>

namespace papper::detail::globals
{

static inline constexpr size_t alignment = 128;
static inline std::atomic<size_t> default_queue_size = 1048576; // 1Mb
static inline constexpr size_t default_flush_buffer_size = 8 * 1024;
static inline constexpr uint64_t default_max_flush_interval_ms = 3000;

}
