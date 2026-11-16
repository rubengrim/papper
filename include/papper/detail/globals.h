#pragma once

#include <atomic>

namespace papper::detail::globals
{

static inline std::atomic<size_t> default_queue_size = 1048576; // 1Mb
static inline const size_t default_flush_buffer_size = 8 * 1024;
static inline const uint64_t default_max_flush_interval_ms = 3000;

}
