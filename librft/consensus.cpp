#include <librft/consensus.h>
#include <libutils/logger.h>
#include <libutils/utils.h>
#include <sstream>

using namespace rft;
using namespace utils::logging;

namespace {
inline std::mt19937 make_seeded_engine() {
  std::random_device r;
  std::seed_seq seed{r(), r(), r(), r(), r(), r(), r(), r()};
  return std::mt19937(seed);
}
} // namespace

consensus::consensus(const node_settings &ns,
                     abstract_cluster *cluster,
                     const logdb::journal_ptr &jrn,
                     abstract_consensus_consumer *consumer)
    : _consumer(consumer)
    , _settings(ns)
    , _cluster(cluster)
    , _jrn(jrn)
    , _rnd_eng(make_seeded_engine()) {
  ENSURE(_consumer != nullptr);
  logger_info("node ", ns.name(),
              ": election_timeout(ms)=", ns.election_timeout().count());

  _self_addr.set_name(_settings.name());

  _state.start_time = clock_t::now().time_since_epoch().count();
  update_next_heartbeat_interval();
  _state.last_heartbeat_time = clock_t::now();
}

void consensus::set_cluster(abstract_cluster *cluster) {
  std::lock_guard<std::mutex> lg(_locker);
  _cluster = cluster;
}

void consensus::update_next_heartbeat_interval() {
  const auto total_mls = _settings.election_timeout().count();
  double k1 = 1.5, k2 = 2.0;
  if (_state.node_kind == NODE_KIND::LEADER) {
    k1 = 0.5;
    k2 = 1;
  }
  if (_state.node_kind == NODE_KIND::ELECTION) {
    k1 = 0.5;
    k2 = 1.0;
  }
  if (_state.node_kind == NODE_KIND::CANDIDATE) {
    k1 = 1.0;
    k2 = 1.0 * _state.election_round;
  }

  std::uniform_int_distribution<uint64_t> distr(uint64_t(total_mls * k1),
                                                uint64_t(total_mls * k2));

  _state.next_heartbeat_interval = std::chrono::milliseconds(distr(_rnd_eng));
  /*logger_info("node: ", _settings.name(), ": next heartbeat is ",
              _state.next_heartbeat_interval.count());*/
}

append_entries consensus::make_append_entries(const entries_kind_t kind) const noexcept {
  append_entries ae;
  ae.term = _state.term;
  ae.starttime = _state.start_time;
  ae.leader = _state.leader;
  ae.kind = kind;
  ae.prev = _jrn->prev_rec();
  ae.commited = _jrn->commited_rec();
  return ae;
}

void consensus::send(const entries_kind_t kind) {
  _cluster->send_all(_self_addr, make_append_entries(kind));
}

void consensus::send(const cluster_node &to, const entries_kind_t kind) {
  _cluster->send_to(_self_addr, to, make_append_entries(kind));
}

void consensus::lost_connection_with(const cluster_node &addr) {
  std::lock_guard<std::mutex> lg(_locker);
  logger_info("node: ", _settings.name(), ": lost connection with ", addr);
  _log_state.erase(addr);
  _last_sended.erase(addr);
  _state.votes_to_me.erase(addr);
}

void consensus::on_heartbeat(const cluster_node &from, const append_entries &e) {
  const auto old_s = _state;
  const auto ns = node_state_t::on_append_entries(_state, from, _jrn.get(), e);
  _state = ns;
  _state.last_heartbeat_time = clock_t::now();
  if (old_s.leader.is_empty() || old_s.leader != _state.leader) {
    _log_state.clear();
    bool reset = false;
    logdb::reccord_info to_erase{};
    if (e.prev.lsn != _jrn->prev_rec().lsn) {
      logger_info("node: ", _settings.name(), ": erase prev ", _jrn->prev_rec(), " => ",
                  e.prev);
      reset = true;
      to_erase = e.prev;
    }
    if (e.commited.lsn != _jrn->commited_rec().lsn) {
      reset = true;
      if (to_erase.is_empty() || e.commited.lsn < to_erase.lsn) {
        logger_info("node: ", _settings.name(), ": erase commited ", _jrn->commited_rec(),
                    " => ", e.commited);
        to_erase = e.commited;
      }
    }
    if (reset) {
      _jrn->erase_all_after(to_erase);
      _consumer->reset();
      auto f = [this](const logdb::log_entry &le) { _consumer->apply_cmd(le.cmd); };
      _jrn->visit(f);
      logger_info("node: ", _settings.name(), ": consumer restored ");
    }
    send(from, entries_kind_t::HELLO);
  }
}

void consensus::on_vote(const cluster_node &from, const append_entries &e) {
  const auto old_s = _state;
  const changed_state_t change_state_v
      = node_state_t::on_vote(_state, _settings, _self_addr, _cluster->size(), from, e);

  const node_state_t ns = change_state_v.new_state;
  _state = ns;
  if (_state.node_kind != old_s.node_kind) {
    if (old_s.node_kind == NODE_KIND::CANDIDATE
        && ns.node_kind == NODE_KIND::LEADER) { // if CANDIDATE => LEADER
      std::stringstream ss;
      auto sz = ns.votes_to_me.size();
      for (auto &&v : ns.votes_to_me) {
        ss << v.name() << ", ";
      }
      logger_info("node: ", _settings.name(), ": quorum. i'am new leader with ", sz,
                  " voices - ", ss.str());
      _state.votes_to_me.clear();
      _log_state[_self_addr] = _jrn->prev_rec();
    } else {
      logger_info("node: ", _settings.name(), ": change state ", old_s, " => ", _state);
    }
  }

  switch (change_state_v.notify) {
  case NOTIFY_TARGET::ALL: {
    send(entries_kind_t::VOTE);
    break;
  }
  case NOTIFY_TARGET::SENDER: {
    send(from, entries_kind_t::VOTE);
    break;
  }
  case NOTIFY_TARGET::NOBODY: {
    break;
  }
  default: {
    NOT_IMPLEMENTED;
    break;
  }
  }
  return;
}

void consensus::recv(const cluster_node &from, const append_entries &e) {
  std::lock_guard<std::mutex> l(_locker);
  if (e.term < _state.term) {
    return;
  }
  /// if leader receive message from follower with other leader,
  /// but with new election term.
  if (e.kind != entries_kind_t::VOTE && _self_addr == _state.leader
      && e.term > _state.term && !e.leader.is_empty()) {
    logger_info("node: ", _settings.name(), ": change state to follower");
    _state.leader.clear();
    _state.change_state(NODE_KIND::FOLLOWER, e.term, e.leader);
    send(e.leader, entries_kind_t::HELLO);
    _log_state.clear();
    _state.votes_to_me.clear();
    return;
  }

  switch (e.kind) {
  case entries_kind_t::HEARTBEAT: {
    on_heartbeat(from, e);

    break;
  }
  case entries_kind_t::HELLO: {
    logger_info("node: ", _settings.name(), ": log_state:", _log_state[from], " => ",
                e.prev);
    _log_state[from] = e.prev;

    replicate_log();
    break;
  }
  case entries_kind_t::VOTE: {
    on_vote(from, e);
    _log_state[from] = e.prev;
    break;
  }
  case entries_kind_t::APPEND: {
    on_append_entries(from, e);
    break;
  }
  case entries_kind_t::ANSWER_OK: {
    on_answer_ok(from, e);
    break;
  }
  default:
    NOT_IMPLEMENTED;
  };
}

void consensus::on_append_entries(const cluster_node &from, const append_entries &e) {
  const auto ns = node_state_t::on_append_entries(_state, from, _jrn.get(), e);

  if (ns.term != _state.term) {
    _state = ns;
    _log_state.clear();
    return;
  }
  _state.last_heartbeat_time = clock_t::now();

  auto self_prev = _jrn->prev_rec();
  if (e.prev != self_prev && !self_prev.is_empty()) {
    logger_fatal("node: ", _settings.name(), ": wrong entry from:", from, " ", e.prev,
                 ", ", self_prev);
    send(from, entries_kind_t::ANSWER_FAILED);
    return;
  }

  auto ae = make_append_entries(entries_kind_t::ANSWER_OK);
  // TODO add check prev,cur,commited
  if (!e.cmd.is_empty() && e.term == _state.term) {
    ENSURE(!e.current.is_empty());
    logger_info("node: ", _settings.name(), ": new entry from:", from,
                " cur:", e.current);

    if (e.current == _jrn->prev_rec()) {
      logger_info("node: ", _settings.name(), ": duplicates");
    } else {
      logger_info("node: ", _settings.name(), ": write to journal ");
      logdb::log_entry le;
      le.term = _state.term;
      le.cmd = e.cmd;
      ae.current = _jrn->put(le);
    }
  }

  if (!e.commited.is_empty()) { /// commit uncommited
    if (_jrn->commited_rec() != e.commited) {
      auto to_commit = _jrn->first_uncommited_rec();
      // ENSURE(!to_commit.is_empty());
      if (!to_commit.is_empty()) {

        auto i = to_commit.lsn;
        while (i <= e.commited.lsn && i <= _jrn->prev_rec().lsn) {
          logger_info("node: ", _settings.name(), ": commit entry from ", from,
                      " { lsn:", i, "}");
          _jrn->commit(i++);
          auto commited = _jrn->commited_rec();
          ae.commited = commited;
          auto le = _jrn->get(commited.lsn);
          _consumer->apply_cmd(le.cmd);
        }
      }
    }
  }

  /// Pong for "heartbeat"

  _cluster->send_to(_self_addr, from, ae);

  _state.last_heartbeat_time = clock_t::now();
}

void consensus::on_answer_ok(const cluster_node &from, const append_entries &e) {
  logger_info("node: ", _settings.name(), ": answer from:", from, " cur:", e.current,
              ", prev", e.prev, ", ci:", e.commited);

  // TODO check for current>_last_for_cluster[from];
  if (!e.current.is_empty()) {
    _log_state[from] = e.current;
  }
  const auto quorum = _cluster->size() * _settings.append_quorum();

  std::unordered_map<logdb::reccord_info, size_t> count(_log_state.size());

  auto commited = _jrn->commited_rec();
  for (auto &kv : _log_state) {
    auto recinfo = kv.second;
    bool is_new = recinfo.lsn > commited.lsn || commited.is_empty();
    if (!recinfo.is_empty() && is_new) {
      auto it = count.find(recinfo);
      if (it == count.end()) {
        count.insert(std::make_pair(recinfo, size_t(1)));
      } else {
        it->second += 1;
      }
    }
  }

  for (auto &kv : count) {
    auto target = kv.first;
    auto cnt = kv.second;
    logger_info("node: ", _settings.name(), ": append votes: ", cnt, " quorum: ", quorum);

    if (size_t(cnt) >= quorum) {
      logger_info("node: ", _settings.name(), ": append quorum.");
      commit_reccord(target);
    }
  }
  replicate_log();
}

void consensus::commit_reccord(const logdb::reccord_info &target) {
  _jrn->commit(target.lsn);

  auto ae = make_append_entries(entries_kind_t::APPEND);
  _consumer->apply_cmd(_jrn->get(ae.commited.lsn).cmd);

  _cluster->send_all(_self_addr, ae);
}

void consensus::heartbeat() {
  std::lock_guard<std::mutex> l(_locker);

  if (_state.node_kind != NODE_KIND::LEADER && _state.is_heartbeat_missed()) {
    // logger_info("node: ", _settings.name(), ": heartbeat");
    const auto old_s = _state;
    const auto ns = node_state_t::heartbeat(_state, _self_addr, _cluster->size());
    _state = ns;

    logger_info("node: ", _settings.name(), ": {", _state.node_kind, "} => - ",
                _state.leader);
  }

  if (_state.node_kind == NODE_KIND::CANDIDATE || _state.node_kind == NODE_KIND::LEADER) {
    auto ae = make_append_entries();
    if (_state.node_kind == NODE_KIND::LEADER) { /// CANDIDATE => LEADER
      // logger_info("node: ", _settings.name(), ": heartbeat");
      ae.kind = entries_kind_t::HEARTBEAT;
      if (_log_state.find(_self_addr) == _log_state.end()) {
        _log_state[_self_addr] = _jrn->prev_rec();
      }
    } else { /// CANDIDATE => CANDIDATE
      _log_state.clear();
      _log_state[_self_addr] = _jrn->prev_rec();
      _last_sended.clear();
      ae.kind = entries_kind_t::VOTE;
    }
    _cluster->send_all(_self_addr, ae);
  }
  update_next_heartbeat_interval();
}

void consensus::replicate_log() {
  // TODO check if log is empty.
  auto self_log_state = _log_state[_self_addr];
  for (auto &kv : _log_state) {
    if (kv.first == _self_addr) {
      continue;
    }
    logger_info("node: ", _settings.name(), ": check replication for ", kv.first,
                " => lsn:", kv.second.lsn, " < self.lsn:", self_log_state.lsn,
                "==", kv.second.lsn < self_log_state.lsn);
    if (kv.second.is_empty() || kv.second.lsn < self_log_state.lsn) {
      auto lsn_to_replicate = kv.second.lsn;
      if (kv.second.is_empty()) {
        lsn_to_replicate = _jrn->first_rec().lsn;
      } else {
        ++lsn_to_replicate; // we need a next record;
      }
      if (_last_sended[kv.first].lsn == lsn_to_replicate) {
        continue;
      }
      auto ae = make_append_entries(rft::entries_kind_t::APPEND);
      auto cur = _jrn->get(lsn_to_replicate);
      ae.current.lsn = lsn_to_replicate;
      ae.current.term = cur.term;
      ae.cmd = cur.cmd;
      if (!kv.second.is_empty()) {
        auto prev = _jrn->get(kv.second.lsn);
        ae.prev.lsn = kv.second.lsn;
        ae.prev.term = kv.second.term;
      }
      _last_sended[kv.first] = ae.current;
      logger_info("node: ", _settings.name(), ": replicate ", ae.current, " to ",
                  kv.first);
      _cluster->send_to(_self_addr, kv.first, ae);
    }
  }
}

void consensus::add_command(const command &cmd) {
  // TODO global lock for this method. a while cmd not in consumer;
  std::lock_guard<std::mutex> lg(_locker);
  ENSURE(!cmd.is_empty());
  logdb::log_entry le;
  le.cmd = cmd;
  le.term = _state.term;

  auto ae = make_append_entries(rft::entries_kind_t::APPEND);
  auto current = _jrn->put(le);
  ae.current = current;
  ae.cmd = cmd;

  _log_state[_self_addr] = current;
  logger_info("node: ", _settings.name(), ": add_command  {", current.term, ", ",
              current.lsn, "}");
  if (_cluster->size() != size_t(1)) {
    for (auto &kv : _log_state) {
      // we or not replicated yet
      if (kv.first == _self_addr || (kv.second.lsn + 1) != ae.current.lsn) {
        continue;
      }
      _last_sended[kv.first] = ae.current;
      _cluster->send_to(_self_addr, kv.first, ae);
    }
  } else {
    commit_reccord(_jrn->first_uncommited_rec());
  }
}