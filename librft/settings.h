#pragma once

#include <librft/utils/utils.h>
#include <chrono>
#include <string>

namespace rft {
class node_settings {
  PROPERTY(std::string, name);
  PROPERTY(std::chrono::milliseconds, period);
};
}; // namespace rft