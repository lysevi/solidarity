#include <solidarity/raft.h>
#include <solidarity/utils/logger.h>
#include <solidarity/utils/utils.h>
#include <sstream>

using namespace solidarity;

namespace {
inline std::mt19937 make_seeded_engine() {
  std::random_device r;
  std::seed_seq seed{r(), r(), r(), r(), r(), r(), r(), r()};
  return std::mt19937(seed);
}
} // namespace

raft::raft(const raft_settings &ns,
           abstract_cluster *cluster,
           const logdb::journal_ptr &jrn,
           abstract_state_machine *state_machine,
           utils::logging::abstract_logger_ptr logger)
    : _state_machine(state_machine)
    , _rnd_eng(make_seeded_engine())
    , _settings(ns)
    , _cluster(cluster)
    , _jrn(jrn) {
  if (logger != nullptr) {
    _logger = std::make_shared<utils::logging::prefix_logger>(logger, "[raft] ");
  } else {
    auto log_prefix = utils::strings::args_to_string("node ", ns.name(), ": ");

    _logger = std::make_shared<utils::logging::prefix_logger>(
        utils::logging::logger_manager::instance()->get_shared_logger(), log_prefix);
  }

  ENSURE(_state_machine != nullptr);

  _settings.dump_to_log(_logger.get());

  _self_addr.set_name(_settings.name());

  _state.start_time = high_resolution_clock_t::now().time_since_epoch().count();
  update_next_heartbeat_interval();
  _state.last_heartbeat_time = high_resolution_clock_t::now();

  // state_machine->reset();
  auto from = _jrn->restore_start_point();
  auto to = _jrn->commited_rec();
  if (!from.is_empty() && !to.is_empty()) {
    for (auto i = from.lsn; i <= to.lsn; ++i) {
      auto rec = jrn->get(i);
      add_reccord(rec);
    }
  }
}

void raft::set_cluster(abstract_cluster *cluster) {
  std::lock_guard lg(_locker);
  _cluster = cluster;
}

void raft::update_next_heartbeat_interval() {
  const auto total_mls = _settings.election_timeout().count();
  uint64_t hbi = 0;
  switch (_state.node_kind) {
  case NODE_KIND::LEADER: {
    hbi = uint64_t(total_mls / 2.0);
    break;
  }
  case NODE_KIND::FOLLOWER: {
    hbi = uint64_t(total_mls * 1.5);
    break;
  }
  case NODE_KIND::ELECTION: {
    auto k1 = 0.5;
    auto k2 = 1.0;
    std::uniform_int_distribution<uint64_t> distr(uint64_t(total_mls * k1),
                                                  uint64_t(total_mls * k2));
    hbi = distr(_rnd_eng);
    break;
  }
  case NODE_KIND::CANDIDATE: {
    ENSURE(_state.election_round > 0);
    auto k1 = 1.0;
    auto k2 = 1.0 * _state.election_round;
    std::uniform_int_distribution<uint64_t> distr(uint64_t(total_mls * k1),
                                                  uint64_t(total_mls * k2));
    hbi = distr(_rnd_eng);
    break;
  }
  }

  _state.next_heartbeat_interval = std::chrono::milliseconds(hbi);
  /*logger_info("node: ", _settings.name(), ": next heartbeat is ",
              _state.next_heartbeat_interval.count());*/
}

append_entries raft::make_append_entries(const ENTRIES_KIND kind) const noexcept {
  append_entries ae;
  ae.term = _state.term;
  ae.starttime = _state.start_time;
  ae.leader = _state.leader;
  ae.kind = kind;
  ae.prev = _jrn->prev_rec();
  ae.commited = _jrn->commited_rec();
  return ae;
}

append_entries raft::make_append_entries(const logdb::index_t lsn_to_replicate,
                                         const logdb::index_t prev_lsn) {
  auto ae = make_append_entries(solidarity::ENTRIES_KIND::APPEND);
  auto cur = _jrn->get(lsn_to_replicate);

  if (cur.kind == logdb::LOG_ENTRY_KIND::APPEND) {
    ae.cmd = cur.cmd;
  }

  ae.current.kind = cur.kind;
  ae.current.lsn = lsn_to_replicate;
  ae.current.term = cur.term;

  if (prev_lsn != logdb::UNDEFINED_INDEX) {
    auto prev = _jrn->info(prev_lsn);
    ae.prev = prev;
  } else {
    ae.prev = logdb::reccord_info{};
  }
  return ae;
}

void raft::send_all(const ENTRIES_KIND kind) {
  _cluster->send_all(_self_addr, make_append_entries(kind));
}

void raft::send(const node_name &to, const ENTRIES_KIND kind) {
  ENSURE(to != _self_addr);
  _cluster->send_to(_self_addr, to, make_append_entries(kind));
}

void raft::lost_connection_with(const node_name &addr) {
  std::lock_guard lg(_locker);
  _logger->info("lost connection with ", addr);
  _logs_state.erase(addr);
  _last_sended.erase(addr);
  _state.votes_to_me.erase(addr);
}

void raft::new_connection_with(const solidarity::node_name & /*addr*/) {
  // TODO need implementation
}

void raft::on_heartbeat(const node_name &from, const append_entries &e) {
  const auto old_s = _state;
  const auto ns = raft_state_t::on_append_entries(_state, from, _jrn.get(), e);
  _state = ns;
  if (old_s.leader.is_empty() || old_s.leader != _state.leader
      || old_s.node_kind != _state.node_kind) {
    _logger->info("on_heartbeat. change leader from: ",
                  old_s,
                  " => ",
                  _state,
                  " append_entries: ",
                  e);
    _logs_state.clear();
    _logger->info("send hello to ", from);
    send(from, ENTRIES_KIND::HELLO);
  }
  if (from == _state.leader) {
    _logger->dbg("on_heartbeat: update last_heartbeat from ", from);
    _state.last_heartbeat_time = high_resolution_clock_t::now();
  }
}

void raft::on_vote(const node_name &from, const append_entries &e) {
  const auto old_s = _state;
  const changed_state_t change_state_v = raft_state_t::on_vote(
      _state, _settings, _self_addr, _jrn->commited_rec(), _cluster->size(), from, e);

  const raft_state_t ns = change_state_v.new_state;
  _state = ns;
  if (_state.node_kind != old_s.node_kind) {
    if (old_s.node_kind == NODE_KIND::CANDIDATE
        && ns.node_kind == NODE_KIND::LEADER) { // if CANDIDATE => LEADER
      std::stringstream ss;
      auto sz = ns.votes_to_me.size();
      for (auto &&v : ns.votes_to_me) {
        ss << v.name() << ", ";
      }
      _logger->info("quorum. i'am new leader with ", sz, " voices - ", ss.str());
      _state.votes_to_me.clear();
      _logs_state[_self_addr].prev = _jrn->prev_rec();
    } else {
      _logger->info("on_vote. change state from:",
                    from,
                    ", ae: ",
                    e,
                    " state:",
                    old_s,
                    " => ",
                    _state);
      if (from == _state.leader) {
        _logger->dbg("on_vote: update last_heartbeat from ", from);
        _state.last_heartbeat_time = high_resolution_clock_t::now();
      }
    }
  }

  switch (change_state_v.notify) {
  case NOTIFY_TARGET::ALL: {
    send_all(ENTRIES_KIND::VOTE);
    break;
  }
  case NOTIFY_TARGET::SENDER: {
    send(from, ENTRIES_KIND::VOTE);
    break;
  }
  case NOTIFY_TARGET::NOBODY: {
    break;
  }
  }
}

void raft::recv(const node_name &from, const append_entries &e) {
  std::lock_guard l(_locker);
#ifdef DOUBLE_CHECKS
  if (from == _self_addr) {
    _logger->fatal("from==self!!! ", e);
  }
#endif

  if (e.term < _state.term) {
    return;
  }
  /// if leader receive message from follower with other leader,
  /// but with new election term.
  if (e.kind != ENTRIES_KIND::VOTE && _self_addr == _state.leader && e.term > _state.term
      && !e.leader.is_empty()) {
    _logger->info("change state to follower");
    _state.leader.clear();
    _state.change_state(NODE_KIND::FOLLOWER, e.term, e.leader);
    _logger->info("send hello to ", from);
    send(e.leader, ENTRIES_KIND::HELLO);
    _logs_state.clear();
    _state.votes_to_me.clear();
  }

  switch (e.kind) {
  case ENTRIES_KIND::HEARTBEAT: {
    on_heartbeat(from, e);

    break;
  }
  case ENTRIES_KIND::HELLO: {
    auto it = _logs_state.find(from);

    /// TODO what if the sender clean log and resend hello? it is possible?
    /// TODO remove from last sended, clear log state for 'from', write new values.
    if (it == _logs_state.end() || it->second.prev.is_empty()) {
      _logger->info(
          "hello. update log_state[", from, "]:", _logs_state[from].prev, " => ", e.prev);
      _logs_state[from].prev = e.prev;
    }
    // replicate_log();
    break;
  }
  case ENTRIES_KIND::VOTE: {
    on_vote(from, e);
    _logs_state[from].prev = e.prev;
    break;
  }
  case ENTRIES_KIND::APPEND: {
    on_append_entries(from, e);
    break;
  }
  case ENTRIES_KIND::ANSWER_OK: {
    on_answer_ok(from, e);
    break;
  }
  case ENTRIES_KIND::ANSWER_FAILED: {
    on_answer_failed(from, e);
    break;
  }
  }
}

void raft::on_append_entries(const node_name &from, const append_entries &e) {
  const auto ns = raft_state_t::on_append_entries(_state, from, _jrn.get(), e);

  if (ns.term != _state.term) {
    _state = ns;
    _logs_state.clear();
    return;
  }
  auto self_prev = _jrn->prev_rec();
  if (e.current != self_prev && e.prev != self_prev && !self_prev.is_empty()) {
    if (e.current.lsn == 0) {
    if (e.current.lsn == 0 /*|| e.current.kind == logdb::LOG_ENTRY_KIND::SNAPSHOT*/) {
      _logger->info("clear journal!");
      _jrn->erase_all_after(logdb::index_t{-1});
      ENSURE(_jrn->size() == size_t(0));
    } else {
      auto info = _jrn->info(e.prev.lsn);
      if (!info.is_empty() && info.term == e.prev.term && info.kind == e.prev.kind) {
        _logger->info("erase all after ", info);
        _jrn->erase_all_after(info.lsn);
      } else {
        _logger->info(
            "wrong entry from:", from, " e.prev:", e.prev, ", self.prev:", self_prev);
        send(from, ENTRIES_KIND::ANSWER_FAILED);
        return;
      }
    }
    _state_machine->reset();
  }

  // TODO add check prev,cur,commited
  if ((!e.current.is_empty() || e.current.kind == logdb::LOG_ENTRY_KIND::SNAPSHOT)
      && e.term == _state.term) {
    ENSURE(!e.current.is_empty() || e.current.kind == logdb::LOG_ENTRY_KIND::SNAPSHOT);
    _logger->info("new entry from:", from, " cur:", e.current, " self_prev:", self_prev);

    if (e.current == self_prev) {
      _logger->info("duplicates");
    } else {
      logdb::log_entry le;
      le.term = e.current.term;
      if (!e.cmd.is_empty()) {
        le.cmd = e.cmd;
      } else {
        if (e.cmd.is_empty() && e.current.kind == logdb::LOG_ENTRY_KIND::SNAPSHOT) {
          _logger->info("create snapshot");
          le.cmd = _state_machine->snapshot();
          le.kind = logdb::LOG_ENTRY_KIND::SNAPSHOT;
        }
      }
      if (!le.cmd.is_empty()) {
        _logger->info("write to journal ");
        _jrn->put(e.current.lsn, le);
      }
    }
  }

  if (!e.commited.is_empty()) { /// commit uncommited
    if (_jrn->commited_rec() != e.commited) {

      commit_reccord(e.commited);
    }
  }

  /// Pong for "heartbeat"
  auto ae = make_append_entries(ENTRIES_KIND::ANSWER_OK);
  _cluster->send_to(_self_addr, from, ae);
}

void raft::on_answer_ok(const node_name &from, const append_entries &e) {
  _logger->info("answer 'OK' from:",
                from,
                " cur:",
                e.current,
                ", prev",
                e.prev,
                ", ci:",
                e.commited);

  // TODO check for current>_last_for_cluster[from];
  if (!e.prev.is_empty()) {
    _logs_state[from].prev = e.prev;
    if (_logs_state[from].direction == RDIRECTION::BACKWARDS) {
      _logs_state[from].direction = RDIRECTION::FORWARDS;
    }
  }
  const size_t quorum = quorum_for_cluster(_cluster->size(), _settings.append_quorum());

  ENSURE(quorum <= _cluster->size());

  std::unordered_map<logdb::reccord_info, size_t> count(_logs_state.size());

  auto commited = _jrn->commited_rec();
  for (auto &kv : _logs_state) {
    auto recinfo = kv.second.prev;
    bool is_new = recinfo.lsn > commited.lsn || commited.is_empty();
    if (!recinfo.is_empty() && is_new) {
      if (auto it = count.find(recinfo); it == count.end()) {
        count.insert(std::make_pair(recinfo, size_t(1)));
      } else {
        it->second += 1;
      }
    }
  }

  for (auto &kv : count) {
    auto target = kv.first;
    auto cnt = kv.second;
    _logger->info("append votes: ", cnt, " quorum: ", quorum);

    if (size_t(cnt) >= quorum) {
      _logger->info("append quorum.");
      commit_reccord(target);
      send_all(ENTRIES_KIND::APPEND);
    }
  }
  // replicate_log();
}

void raft::on_answer_failed(const node_name &from, const append_entries &e) {
  _logger->warn("answer 'FAILED' from:",
                from,
                " cur:",
                e.current,
                ", prev",
                e.prev,
                ", ci:",
                e.commited);
  if (auto it = _logs_state.find(from); it == _logs_state.end()) {
    _logs_state[from].prev = e.prev;
  } else {
    it->second.direction = RDIRECTION::BACKWARDS;
    auto last_sended_it = _last_sended.find(from);
    ENSURE(last_sended_it != _last_sended.end());
    if (it->second.prev.lsn != logdb::UNDEFINED_INDEX) { /// move replication log backward
      it->second.prev.lsn = last_sended_it->second.lsn - 1;
    }
  }
}

void raft::commit_reccord(const logdb::reccord_info &target) {
  auto to_commit = _jrn->first_uncommited_rec();
  if (!to_commit.is_empty()) {

    auto i = to_commit.lsn;
    while (i <= target.lsn && i <= _jrn->prev_rec().lsn) {
      _logger->info("commit entry lsn:", i);
      auto info = _jrn->info(i);
      _jrn->commit(i);
      auto commited = _jrn->commited_rec();
      auto le = _jrn->get(commited.lsn);

      if (info.kind == logdb::LOG_ENTRY_KIND::APPEND) {
        ENSURE(_state_machine != nullptr);
        add_reccord(le);
      } else {
        auto erase_point = _jrn->info(i - 1);
        _logger->info("erase all to ", erase_point);
        _jrn->erase_all_to(erase_point.lsn);
      }
      ++i;
    }
  }
}

void raft::heartbeat() {
  std::lock_guard l(_locker);

  if (!_state.is_heartbeat_missed()) {
    return;
  }

  if (_state.node_kind != NODE_KIND::LEADER) {
    const auto old_s = _state;
    ENSURE(_cluster != nullptr);
    const auto ns = raft_state_t::heartbeat(_state, _self_addr, _cluster->size());
    _state = ns;

    _logger->info("heartbeat. ", old_s, " => ", _state);
  }

  if (_state.node_kind == NODE_KIND::CANDIDATE || _state.node_kind == NODE_KIND::LEADER) {

    if (_state.node_kind == NODE_KIND::LEADER) { /// CANDIDATE => LEADER
      if (_logs_state.find(_self_addr) == _logs_state.end()) {
        _logs_state[_self_addr].prev = _jrn->prev_rec();
      }
      replicate_log();
    } else { /// CANDIDATE => CANDIDATE
      _logs_state.clear();
      _logs_state[_self_addr].prev = _jrn->prev_rec();
      _last_sended.clear();
      auto ae = make_append_entries();
      ae.kind = ENTRIES_KIND::VOTE;
      _logger->info("send vote to all.");
      _cluster->send_all(_self_addr, ae);
    }
  }
  update_next_heartbeat_interval();
}

void raft::replicate_log() {
  _logger->info("log replication");
  auto self_log_state = _logs_state[_self_addr];
  auto all = _cluster->all_nodes();

  auto jrn_sz = _jrn->size();
  auto jrn_is_empty = jrn_sz == size_t(0);
  auto first_jrn_record = _jrn->first_rec();

  for (const auto &naddr : all) {
    if (naddr == _self_addr) {
      continue;
    }

    auto kv = _logs_state.find(naddr);
    if (jrn_is_empty || kv == _logs_state.end()) {
      send(naddr, ENTRIES_KIND::HEARTBEAT);
      continue;
    }

    auto last_sended_it = _last_sended.find(kv->first);

    bool is_append = false;
    if (kv->second.prev.lsn_is_empty() || kv->second.prev != self_log_state.prev) {
      auto lsn_to_replicate = kv->second.prev.lsn;
      if (kv->second.prev.lsn_is_empty()) {
        lsn_to_replicate = _jrn->first_rec().lsn;
      } else {
        if (kv->second.prev.lsn >= self_log_state.prev.lsn) {
          lsn_to_replicate = _jrn->prev_rec().lsn;
        } else {
          switch (kv->second.direction) {
          case RDIRECTION::FORWARDS:
            if (lsn_to_replicate != _jrn->prev_rec().lsn) {
              if (lsn_to_replicate < first_jrn_record.lsn) {
                _logger->info("lsn_to_replicate < _jrn->first_rec().lsn");
                lsn_to_replicate = _jrn->restore_start_point().lsn;
              } else {
                ++lsn_to_replicate; // we need a next record;
              }
            }
            break;
          case RDIRECTION::BACKWARDS:
            break;
          }
        }
      }

      if (last_sended_it == _last_sended.end()
          || last_sended_it->second.lsn != lsn_to_replicate) {
        _logger->info("try replication for ",
                      kv->first,
                      " => prev: ",
                      kv->second.prev,
                      " != self.prev:",
                      self_log_state.prev);

        auto ae = make_append_entries(lsn_to_replicate,
                                      lsn_to_replicate > 0 ? lsn_to_replicate - 1
                                                           : logdb::UNDEFINED_INDEX);

        _last_sended[kv->first] = ae.current;
        _logger->info("replicate cur:",
                      ae.current,
                      " prev:",
                      ae.prev,
                      " ci:",
                      ae.commited,
                      " to ",
                      kv->first);
        ENSURE(!ae.current.is_empty()
               || ae.current.kind == logdb::LOG_ENTRY_KIND::SNAPSHOT);
        _cluster->send_to(_self_addr, kv->first, ae);
        is_append = true;
      }
    }
    if (!is_append) {
      send(naddr, ENTRIES_KIND::HEARTBEAT);
    }
  }
}

ERROR_CODE raft::add_command_impl(const command &cmd, logdb::LOG_ENTRY_KIND k) {
  ENSURE(!cmd.is_empty());
  if (k != logdb::LOG_ENTRY_KIND::SNAPSHOT && !_state_machine->can_apply(cmd)) {
    return ERROR_CODE::STATE_MACHINE_CAN_T_APPLY_CMD;
  }
  logdb::log_entry le;
  le.cmd = cmd;
  le.term = _state.term;
  le.kind = k;

  auto current = _jrn->put(le);
  ENSURE(_jrn->size() != size_t(0));

  _logs_state[_self_addr].prev = current;
  _logger->info("add_command: ", current);

  /*if (_cluster->size() == size_t(1)) {
    commit_reccord(_jrn->first_uncommited_rec());
  }*/
  return ERROR_CODE::OK;
}

ERROR_CODE raft::add_command(const command &cmd) {
  // TODO global lock for this method. a while cmd not in state_machine;
  std::lock_guard lg(_locker);

  if (_state.node_kind != NODE_KIND::LEADER) {
    // THROW_EXCEPTION("only leader-node have rights to add commands into the cluster!");
    return ERROR_CODE::NOT_A_LEADER;
  }

  if (_jrn->size() >= _settings.max_log_size()) {
    _logger->info("create snapshot");
    auto res
        = add_command_impl(_state_machine->snapshot(), logdb::LOG_ENTRY_KIND::SNAPSHOT);
    if (res != ERROR_CODE::OK) {
      THROW_EXCEPTION("snapshot save error: ", res);
    }
  }

  return add_command_impl(cmd, logdb::LOG_ENTRY_KIND::APPEND);
}

void raft::add_reccord(const logdb::log_entry &le) {
  switch (le.kind) {
  case logdb::LOG_ENTRY_KIND::APPEND: {
    _state_machine->apply_cmd(le.cmd);
    break;
  }
  case logdb::LOG_ENTRY_KIND::SNAPSHOT: {
    _state_machine->install_snapshot(le.cmd);
    break;
  }
  }
}