#pragma once

#include <libutils/property.h>
#include <chrono>
#include <string>

namespace rft {
class node_settings {
public:
  node_settings() {
    _vote_quorum = 0.5;
    _append_quorum = 1.0;
  }
  PROPERTY(std::string, name);
  PROPERTY(std::chrono::milliseconds, election_timeout);
  PROPERTY(float, vote_quorum);
  PROPERTY(float, append_quorum);
};
}; // namespace rft