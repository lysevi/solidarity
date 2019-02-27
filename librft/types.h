#pragma once
#include <chrono>
#include <cstdint>

namespace rft {
using round_t = uint64_t;
using clock_t = std::chrono::high_resolution_clock;
} // namespace rft