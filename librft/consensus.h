#pragma once

#include <librft/abstract_cluster.h>
#include <librft/exports.h>
#include <librft/journal.h>
#include <librft/settings.h>
#include <chrono>
#include <memory>
#include <random>
#include <set>
#include <shared_mutex>
#include <string>

namespace rft {

using clock_t = std::chrono::high_resolution_clock;

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
  std::set<rft::cluster_node> _election_to_me;
};

inline std::string to_string(const node_state_t &s) {
  std::stringstream ss;
  ss << "{" << to_string(s.round_kind) << ", " << s.round << ", " << to_string(s.leader)
     << "|}";
  return ss.str();
}

class consensus {
public:
  EXPORT consensus(const node_settings &ns, abstract_cluster *cluster,
                   const logdb::journal_ptr &jrn);
  ROUND_KIND state() const { return _nodestate.round_kind; }
  round_t round() const { return _nodestate.round; }
  EXPORT void on_heartbeat();
  EXPORT void recv(const cluster_node &from, const append_entries &e);

  cluster_node get_leader() const {
    std::lock_guard<std::mutex> l(_locker);
    return _nodestate.leader;
  }
  cluster_node self_addr() const {
    std::lock_guard<std::mutex> l(_locker);
    return _self_addr;
  }

protected:
  append_entries make_append_entries() const;
  append_entries make_append_entries_unsafe() const;
  bool is_heartbeat_missed() const;

  void on_vote(const cluster_node &from, const append_entries &e);
  void on_append_entries(const cluster_node &from, const append_entries &e);

  void change_state(const ROUND_KIND s, const round_t r, const cluster_node &leader);
  void change_state(const cluster_node &cn, const round_t r);

  void update_next_heartbeat_interval();

private:
  mutable std::mutex _locker;
  std::mt19937 _rnd_eng;
  std::chrono::milliseconds _next_heartbeat_interval;

  node_settings _settings;
  cluster_node _self_addr;
  abstract_cluster *_cluster;
  logdb::journal_ptr _jrn;

  uint64_t _start_time;

  node_state_t _nodestate;
};

}; // namespace rft