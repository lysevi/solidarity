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

enum class CONSENSUS_STATE { LEADER = 0, FOLLOWER = 1, CANDIDATE = 2, ELECTION = 3 };

inline std::string to_string(const rft::CONSENSUS_STATE s) {
  switch (s) {
  case rft::CONSENSUS_STATE::CANDIDATE:
    return "CANDIDATE";
  case rft::CONSENSUS_STATE::FOLLOWER:
    return "FOLLOWER";
  case rft::CONSENSUS_STATE::LEADER:
    return "LEADER";
  case rft::CONSENSUS_STATE::ELECTION:
    return "ELECTION";
  default:
    return "!!! UNKNOW !!!";
  }
}

class consensus {
public:
  EXPORT consensus(const node_settings &ns, abstract_cluster *cluster,
                   const logdb::journal_ptr &jrn);
  CONSENSUS_STATE state() const { return _state; }
  round_t round() const { return _round; }
  EXPORT void on_heartbeat();
  EXPORT void recv(const cluster_node &from, const append_entries &e);

  cluster_node get_leader() const {
    std::lock_guard<std::mutex> l(_locker);
    return _leader;
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

  void change_state(const CONSENSUS_STATE s, const round_t r, const cluster_node &leader);
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

  CONSENSUS_STATE _state{CONSENSUS_STATE::FOLLOWER};

  uint64_t _start_time;

  round_t _round{0};
  clock_t::time_point _last_heartbeat_time;
  cluster_node _leader;

  std::set<rft::cluster_node> _election_to_me;
  size_t _election_round = 0;
};

}; // namespace rft