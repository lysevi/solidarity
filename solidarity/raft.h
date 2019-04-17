#pragma once

#include <solidarity/abstract_state_machine.h>
#include <solidarity/exports.h>
#include <solidarity/journal.h>
#include <solidarity/raft_settings.h>
#include <solidarity/raft_state.h>
#include <solidarity/utils/logger.h>

#include <memory>
#include <random>
#include <shared_mutex>
#include <unordered_map>

namespace solidarity {

class raft : public abstract_cluster_client {
  enum class RDIRECTION { FORWARDS = 0, BACKWARDS };
  struct log_state_t {
    logdb::reccord_info prev;
    RDIRECTION direction = RDIRECTION::FORWARDS;
  };

public:
  EXPORT raft(const raft_settings &ns,
                   abstract_cluster *cluster,
                   const logdb::journal_ptr &jrn,
                   abstract_state_machine *state_machine,
                   utils::logging::abstract_logger_ptr logger = nullptr);
  raft_state_t state() const {
    std::lock_guard l(_locker);
    return _state;
  }
  raft_state_t &rw_state() { return _state; }

  NODE_KIND kind() const { return _state.node_kind; }
  term_t term() const { return _state.term; }
  logdb::journal_ptr journal() const { return _jrn; }
  abstract_state_machine *state_machine() { return _state_machine; }

  EXPORT void set_cluster(abstract_cluster *cluster);
  EXPORT void heartbeat() override;

  EXPORT void recv(const node_name &from, const append_entries &e) override;
  EXPORT void lost_connection_with(const node_name &addr) override;
  EXPORT void new_connection_with(const solidarity::node_name &addr) override;
  [[nodiscard]] EXPORT ERROR_CODE add_command(const command &cmd) override;

  node_name get_leader() const {
    std::lock_guard l(_locker);
    return _state.leader;
  }
  node_name self_addr() const {
    std::lock_guard l(_locker);
    return _self_addr;
  }

  raft_settings settings() const {
    std::lock_guard l(_locker);
    return _settings;
  }

protected:
  [[nodiscard]] append_entries make_append_entries(const ENTRIES_KIND kind
                                                   = ENTRIES_KIND::APPEND) const
      noexcept;
  [[nodiscard]] append_entries make_append_entries(const logdb::index_t lsn_to_replicate,
                                     const logdb::index_t prev_lsn);
  void send_all(const ENTRIES_KIND kind);
  void send(const node_name &to, const ENTRIES_KIND kind);

  void on_heartbeat(const node_name &from, const append_entries &e);
  void on_vote(const node_name &from, const append_entries &e);
  void on_append_entries(const node_name &from, const append_entries &e);
  void on_answer_ok(const node_name &from, const append_entries &e);
  void on_answer_failed(const node_name &from, const append_entries &e);

  void update_next_heartbeat_interval();

  void commit_reccord(const logdb::reccord_info &target);
  void replicate_log();

  [[nodiscard]] ERROR_CODE add_command_impl(const command &cmd, logdb::LOG_ENTRY_KIND k);
  void add_reccord(const logdb::log_entry &le);
private:
  abstract_state_machine *const _state_machine = nullptr;
  mutable std::mutex _locker;
  std::mt19937 _rnd_eng;

  raft_settings _settings;
  node_name _self_addr;
  abstract_cluster *_cluster;
  logdb::journal_ptr _jrn;

  raft_state_t _state;

  std::unordered_map<node_name, log_state_t> _logs_state;
  std::unordered_map<node_name, logdb::reccord_info> _last_sended;

  utils::logging::abstract_logger_ptr _logger;
};

}; // namespace solidarity