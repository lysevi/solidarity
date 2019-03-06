#pragma once

#include <librft/exports.h>
#include <librft/journal.h>
#include <librft/settings.h>
#include <librft/state.h>

#include <memory>
#include <random>
#include <shared_mutex>
#include <unordered_map>

namespace rft {

class abstract_consensus_consumer {
public:
  virtual void apply_cmd(const command &cmd) = 0;
};

class consensus {
public:
  EXPORT consensus(const node_settings &ns,
                   abstract_cluster *cluster,
                   const logdb::journal_ptr &jrn,
                   abstract_consensus_consumer *consumer);
  NODE_KIND state() const { return _state.node_kind; }
  term_t term() const { return _state.term; }
  EXPORT void on_heartbeat();
  EXPORT void recv(const cluster_node &from, const append_entries &e);
  EXPORT void add_command(const command &cmd);

  cluster_node get_leader() const {
    std::lock_guard<std::mutex> l(_locker);
    return _state.leader;
  }
  cluster_node self_addr() const {
    std::lock_guard<std::mutex> l(_locker);
    return _self_addr;
  }

protected:
  append_entries make_append_entries(const entries_kind_t kind
                                     = entries_kind_t::APPEND) const noexcept;

  void on_vote(const cluster_node &from, const append_entries &e);
  void on_append_entries(const cluster_node &from, const append_entries &e);
  void on_answer(const cluster_node &from, const append_entries &e);

  void update_next_heartbeat_interval();

  void commit_reccord(const logdb::reccord_info &target);
  void replicate_log();

private:
  abstract_consensus_consumer *const _consumer = nullptr;
  mutable std::mutex _locker;
  std::mt19937 _rnd_eng;

  node_settings _settings;
  cluster_node _self_addr;
  abstract_cluster *_cluster;
  logdb::journal_ptr _jrn;

  node_state_t _state;

  std::unordered_map<cluster_node, logdb::reccord_info> _log_state;
  std::unordered_map<cluster_node, logdb::reccord_info> _last_sended;
};

}; // namespace rft