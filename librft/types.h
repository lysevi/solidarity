#pragma once
#include <chrono>
#include <cstdint>

namespace rft {
using term_t = int64_t;
using clock_t = std::chrono::high_resolution_clock;

const term_t UNDEFINED_TERM = -1;
} // namespace rft