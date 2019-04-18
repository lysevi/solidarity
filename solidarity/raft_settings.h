#pragma once

#include <chrono>
#include <solidarity/utils/property.h>
#include <string>

namespace solidarity::utils {
namespace logging {
class abstract_logger;
}
} // namespace solidarity::utils

namespace solidarity {
class raft_settings {
public:
  raft_settings() {
    _vote_quorum = 0.5;
    _append_quorum = 1.0;
    _max_log_size = 100;
    _election_timeout = std::chrono::milliseconds(300);
    _follower_max_timeout = std::chrono::seconds(1);
  }

  void dump_to_log(utils::logging::abstract_logger *const l);

  PROPERTY(std::string, name)
  PROPERTY(std::chrono::milliseconds, election_timeout)
  PROPERTY(std::chrono::milliseconds, follower_max_timeout)
  PROPERTY(float, vote_quorum)
  PROPERTY(float, append_quorum)
  PROPERTY(size_t, max_log_size)
};
}; // namespace solidarity
