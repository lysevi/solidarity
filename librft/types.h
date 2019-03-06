#pragma once
#include <chrono>
#include <cstdint>

namespace rft {
using term_t = uint64_t;
using clock_t = std::chrono::high_resolution_clock;
} // namespace rft