#pragma once

#include <librft/exports.h>
#include <librft/journal.h>
#include <librft/settings.h>
#include <librft/state.h>

#include <memory>
#include <random>
#include <set>
#include <shared_mutex>

namespace rft {

class consensus {
public:
  EXPORT consensus(const node_settings &ns,
                   abstract_cluster *cluster,
                   const logdb::journal_ptr &jrn);
  ROUND_KIND state() const { return _state.round_kind; }
  round_t round() const { return _state.round; }
  EXPORT void on_heartbeat();
  EXPORT void recv(const cluster_node &from, const append_entries &e);

  cluster_node get_leader() const {
    std::lock_guard<std::mutex> l(_locker);
    return _state.leader;
  }
  cluster_node self_addr() const {
    std::lock_guard<std::mutex> l(_locker);
    return _self_addr;
  }

protected:
  append_entries make_append_entries() const;
  append_entries make_append_entries_unsafe() const;

  void on_vote(const cluster_node &from, const append_entries &e);
  void on_append_entries(const cluster_node &from, const append_entries &e);

  void change_state(const ROUND_KIND s, const round_t r, const cluster_node &leader);
  void change_state(const cluster_node &cn, const round_t r);

  void update_next_heartbeat_interval();

private:
  mutable std::mutex _locker;
  std::mt19937 _rnd_eng;

  node_settings _settings;
  cluster_node _self_addr;
  abstract_cluster *_cluster;
  logdb::journal_ptr _jrn;

  node_state_t _state;
};

}; // namespace rft