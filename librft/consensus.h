#pragma once

#include <librft/abstract_cluster.h>
#include <librft/exports.h>
#include <librft/journal.h>
#include <librft/settings.h>
#include <chrono>
#include <memory>
#include <random>
#include <string>

namespace rft {
namespace inner {
std::mt19937 make_seeded_engine() {
  std::random_device r;
  std::seed_seq seed{r(), r(), r(), r(), r(), r(), r(), r()};
  return std::mt19937(seed);
}
} // namespace inner

using clock_t = std::chrono::high_resolution_clock;

enum class CONSENSUS_STATE { LEADER = 0, FOLLOWER = 1, CANDIDATE = 2 };

inline std::string to_string(const rft::CONSENSUS_STATE s) {
  switch (s) {
  case rft::CONSENSUS_STATE::CANDIDATE:
    return "CANDIDATE";
  case rft::CONSENSUS_STATE::FOLLOWER:
    return "FOLLOWER";
  case rft::CONSENSUS_STATE::LEADER:
    return "LEADER";
  default:
    return "!!! UNKNOW !!!";
  }
}

class consensus {
public:
  EXPORT consensus(const node_settings &ns, const cluster_ptr &cluster,
                   const logdb::journal_ptr &jrn);
  CONSENSUS_STATE state() const { return _state; }
  round_t round() const { return _round; }
  EXPORT void on_heartbeat();
  EXPORT void recv(const cluster_node &from, const append_entries &e);

  cluster_node get_leader() const { return _leader_term; }

protected:
  append_entries make_append_entries() const;
  bool is_heartbeat_missed() const;

private:
  std::mt19937 _rnd_eng = inner::make_seeded_engine();
  std::chrono::milliseconds _next_heartbeat_interval;

  node_settings _settings;
  cluster_node _self_addr;
  cluster_ptr _cluster;
  logdb::journal_ptr _jrn;

  CONSENSUS_STATE _state{CONSENSUS_STATE::FOLLOWER};

  uint64_t _start_time;

  round_t _round{0};
  clock_t::time_point _last_heartbeat_time;
  cluster_node _leader_term;
  std::atomic_size_t _election_to_me;
};

}; // namespace rft