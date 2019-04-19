#pragma once
#include <chrono>
#include <cstdint>

namespace solidarity {
using term_t = int64_t;
/// log sequence numbder;
using index_t = int64_t;
using high_resolution_clock_t = std::chrono::high_resolution_clock;

const term_t UNDEFINED_TERM = -1;
const index_t UNDEFINED_INDEX = {-1};

} // namespace solidarity
