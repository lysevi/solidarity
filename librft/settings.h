#pragma once

#include <libutils/utils.h>
#include <chrono>
#include <string>

namespace rft {
class node_settings {
  PROPERTY(std::string, name);
  PROPERTY(std::chrono::milliseconds, election_timeout);
};
}; // namespace rft