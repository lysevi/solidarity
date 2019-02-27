#pragma once

#include <librft/abstract_cluster.h>
#include <librft/exports.h>
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

struct changed_state_t;

struct node_state_t {
  round_t round{0};
  clock_t::time_point last_heartbeat_time;
  std::chrono::milliseconds next_heartbeat_interval;
  cluster_node leader;
  ROUND_KIND round_kind{ROUND_KIND::FOLLOWER};
  size_t election_round = 0;
  std::set<cluster_node> votes_to_me;

  uint64_t start_time;

  node_state_t &operator=(const node_state_t &o) {
    round = o.round;
    last_heartbeat_time = o.last_heartbeat_time;
    next_heartbeat_interval = o.next_heartbeat_interval;
    leader = o.leader;
    round_kind = o.round_kind;
    election_round = o.election_round;
    votes_to_me = o.votes_to_me;
    start_time = o.start_time;
    return *this;
  }

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

  EXPORT static changed_state_t
  on_vote(const node_state_t &self, const cluster_node &self_addr,
          const size_t cluster_size, const cluster_node &from, const append_entries &e);

  EXPORT static node_state_t on_append_entries(const node_state_t &self,
                                               const cluster_node &from,
                                               const append_entries &e);
  EXPORT static node_state_t on_heartbeat(const node_state_t &self,
                                          const cluster_node &self_addr,
                                          const size_t cluster_size);
};

EXPORT std::string to_string(const node_state_t &s);

enum class NOTIFY_TARGET { SENDER, ALL, NOBODY };

struct changed_state_t {
  node_state_t new_state;
  NOTIFY_TARGET notify;
};



} // namespace rft