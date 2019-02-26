#pragma once

#include <librft/abstract_cluster.h>
#include <librft/journal.h>
#include <string>

namespace rft {

enum class ROUND_KIND { LEADER = 0, FOLLOWER = 1, CANDIDATE = 2, ELECTION = 3 };

inline std::string to_string(const rft::ROUND_KIND s) {
  switch (s) {
  case rft::ROUND_KIND::CANDIDATE:
    return "CANDIDATE";
  case rft::ROUND_KIND::FOLLOWER:
    return "FOLLOWER";
  case rft::ROUND_KIND::LEADER:
    return "LEADER";
  case rft::ROUND_KIND::ELECTION:
    return "ELECTION";
  default:
    return "!!! UNKNOW !!!";
  }
}

struct node_state_t {
  round_t round{0};
  clock_t::time_point last_heartbeat_time;
  cluster_node leader;
  ROUND_KIND round_kind{ROUND_KIND::FOLLOWER};
  size_t _election_round = 0;
  std::set<cluster_node> _election_to_me;

  uint64_t start_time;
};

inline std::string to_string(const node_state_t &s) {
  std::stringstream ss;
  ss << "{" << to_string(s.round_kind) << ", " << s.round << ", " << to_string(s.leader)
     << "|}";
  return ss.str();
}

} // namespace rft