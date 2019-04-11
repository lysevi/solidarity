#pragma once

#include <libsolidarity/abstract_cluster.h>
#include <libsolidarity/exports.h>
#include <libsolidarity/journal.h>
#include <libsolidarity/node_kind.h>
#include <libsolidarity/raft_settings.h>
#include <chrono>
#include <string>
#include <unordered_set>

namespace solidarity {

struct changed_state_t;

inline size_t quorum_for_cluster(size_t cluster_size, float quorum) {
  size_t quorum_size = static_cast<size_t>(cluster_size * quorum);
  /// quorum = 50% +1
  if (std::fabs(quorum - 1.0) > 0.0001) { // quorum!=1.0
    quorum_size += 1;
  }
  return quorum_size;
}

struct raft_state_t {
  term_t term = UNDEFINED_TERM;
  clock_t::time_point last_heartbeat_time;
  std::chrono::milliseconds next_heartbeat_interval = {};
  node_name leader;
  NODE_KIND node_kind{NODE_KIND::FOLLOWER};
  size_t election_round = 0;
  std::unordered_set<node_name> votes_to_me;

  uint64_t start_time;

  raft_state_t &operator=(const raft_state_t &o) {
    term = o.term;
    last_heartbeat_time = o.last_heartbeat_time;
    next_heartbeat_interval = o.next_heartbeat_interval;
    leader = o.leader;
    node_kind = o.node_kind;
    election_round = o.election_round;
    votes_to_me = o.votes_to_me;
    start_time = o.start_time;
    last_heartbeat_time = o.last_heartbeat_time;
    return *this;
  }

  bool operator==(const raft_state_t &o) const {
    return term == o.term && last_heartbeat_time == o.last_heartbeat_time
           && next_heartbeat_interval == o.next_heartbeat_interval && leader == o.leader
           && node_kind == o.node_kind && election_round == o.election_round
           && votes_to_me == o.votes_to_me && start_time == o.start_time;
  }

  bool operator!=(const raft_state_t &o) const { return !(*this == o); }

  bool is_heartbeat_missed() const {
    auto now = clock_t::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_heartbeat_time);
    auto r = diff > next_heartbeat_interval;
    if (r) {
      return true;
    } else {
      return false;
    }
  }

  void change_state(const NODE_KIND s, const term_t r, const node_name &leader_);
  void change_state(const node_name &cn, const term_t r);

  EXPORT static changed_state_t on_vote(const raft_state_t &self,
                                        const raft_settings &settings,
                                        const node_name &self_addr,
                                        const logdb::reccord_info commited,
                                        const size_t cluster_size,
                                        const node_name &from,
                                        const append_entries &e);

  EXPORT static raft_state_t on_append_entries(const raft_state_t &self,
                                               const node_name &from,
                                               const logdb::abstract_journal *jrn,
                                               const append_entries &e);
  EXPORT static raft_state_t heartbeat(const raft_state_t &self,
                                       const node_name &self_addr,
                                       const size_t cluster_size);

  EXPORT static bool is_my_jrn_biggest(const raft_state_t &self,
                                       const logdb::reccord_info commited,
                                       const append_entries &e);
};

EXPORT std::string to_string(const raft_state_t &s);

enum class NOTIFY_TARGET { SENDER, ALL, NOBODY };

struct changed_state_t {
  raft_state_t new_state;
  NOTIFY_TARGET notify;
};

} // namespace solidarity