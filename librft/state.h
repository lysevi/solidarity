#pragma once

#include <librft/abstract_cluster.h>
#include <librft/journal.h>
#include <chrono>
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
  std::chrono::milliseconds next_heartbeat_interval;
  cluster_node leader;
  ROUND_KIND round_kind{ROUND_KIND::FOLLOWER};
  size_t election_round = 0;
  std::set<cluster_node> _election_to_me;

  uint64_t start_time;

  bool is_heartbeat_missed() const {
    auto now = clock_t::now();
    auto diff =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_time);
    auto r = diff > next_heartbeat_interval;
    if (r) {
      return true;
    } else {
      return false;
    }
  }

  void change_state(const ROUND_KIND s, const round_t r, const cluster_node &leader_);
  void change_state(const cluster_node &cn, const round_t r);

  static node_state_t on_vote(const node_state_t &self, const cluster_node &self_addr,
                              const size_t cluster_size, const cluster_node &from,
                              const append_entries &e);

  static node_state_t on_append_entries(const node_state_t &self,
                                        const cluster_node &from,
                                        const append_entries &e);
  static node_state_t on_heartbeat(const node_state_t &self,
                                   const cluster_node &self_addr,
                                   const size_t cluster_size);
};

inline std::string to_string(const node_state_t &s) {
  std::stringstream ss;
  ss << "{" << to_string(s.round_kind) << ", " << s.round << ", " << to_string(s.leader)
     << "|}";
  return ss.str();
}

} // namespace rft