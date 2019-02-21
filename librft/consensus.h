#pragma once

#include <librft/abstract_cluster.h>
#include <librft/exports.h>
#include <librft/journal.h>
#include <librft/settings.h>
#include <chrono>
#include <memory>

namespace rft {

using clock_t = std::chrono::high_resolution_clock;

enum class CONSENSUS_STATE { LEADER = 0, FOLLOWER = 1, CANDIDATE = 2 };

class consensus {
public:
  EXPORT consensus(const node_settings &ns, const cluster_ptr &cluster,
                   const logdb::journal_ptr &jrn);
  CONSENSUS_STATE state() const { return _state; }
  round_t round() const { return _round; }
  EXPORT void on_timer();

private:
  node_settings _settings;
  cluster_ptr _cluster;
  logdb::journal_ptr _jrn;
  round_t _round{0};
  CONSENSUS_STATE _state{CONSENSUS_STATE::FOLLOWER};

  clock_t::time_point heartbeat_time;
};

}; // namespace rft