#pragma once

#include <cstdint>
#include <vector>

namespace rft {

struct command {
  bool is_empty() const { return data.empty(); }

  std::vector<std::uint8_t> data;
};

} // namespace rft