#pragma once

#include <cstdint>
#include <vector>

namespace solidarity {

struct command {
  bool is_empty() const { return data.empty(); }

  std::vector<std::uint8_t> data;
};

} // namespace solidarity
