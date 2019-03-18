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

  // consumer->reset();
  auto from = _jrn->restore_start_point();
  auto to = _jrn->commited_rec();
  if (!from.is_empty() && !to.is_empty()) {
    for (auto i = from.lsn; i <= to.lsn; ++i) {
      auto rec = jrn->get(i);
      consumer->apply_cmd(rec.cmd);
    }
  }
}

void consensus::set_cluster(abstract_cluster *cluster) {
  std::lock_guard<std::mutex> lg(_locker);
  _cluster = cluster;
}

void consensus::update_next_heartbeat_interval() {
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
    auto k1 = 1.0;
    auto k2 = 1.0 * _state.election_round;
    std::uniform_int_distribution<uint64_t> distr(uint64_t(total_mls * k1),
                                                  uint64_t(total_mls * k2));
    hbi = distr(_rnd_eng);
    break;
  }
  default:
    NOT_IMPLEMENTED;
  }

  _state.next_heartbeat_interval = std::chrono::milliseconds(hbi);
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

void consensus::send_all(const entries_kind_t kind) {
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
  if (old_s.leader.is_empty() || old_s.leader != _state.leader
      || old_s.node_kind != _state.node_kind) {
    logger_info("node: ", _settings.name(),
                ":on_heartbeat. change leader from: ", old_s.leader, " => ",
                _state.leader);
    _log_state.clear();
    log_fsck(e);
    logger_info("node: ", _settings.name(), ": send hello to ", from);
    send(from, entries_kind_t::HELLO);
  }
}

/// clear uncommited reccord in journal
void consensus::log_fsck(const append_entries &e) {
  /// TODO really need this?
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
}

void consensus::on_vote(const cluster_node &from, const append_entries &e) {
  const auto old_s = _state;
  const changed_state_t change_state_v = node_state_t::on_vote(
      _state, _settings, _self_addr, _jrn->commited_rec(), _cluster->size(), from, e);

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
      logger_info("node: ", _settings.name(), ":on_vote. change state ", old_s, " => ",
                  _state);
    }
  }

  switch (change_state_v.notify) {
  case NOTIFY_TARGET::ALL: {
    send_all(entries_kind_t::VOTE);
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
    log_fsck(e);
    logger_info("node: ", _settings.name(), ": send hello to ", from);
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
    auto it = _log_state.find(from);

    /// TODO what if the sender clean log and resend hello? it is possible?
    // if (it == _log_state.end() || it->second.is_empty())
    {
      logger_info("node: ", _settings.name(), ": hello. update log_state[", from,
                  "]:", _log_state[from], " => ", e.prev);
      _log_state[from] = e.prev;
    }
    // replicate_log();
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
      _jrn->put(le);
    }
  }

  if (!e.commited.is_empty()) { /// commit uncommited
    if (_jrn->commited_rec() != e.commited) {

      commit_reccord(e.commited);
    }
  }

  /// Pong for "heartbeat"
  auto ae = make_append_entries(entries_kind_t::ANSWER_OK);
  _cluster->send_to(_self_addr, from, ae);

  _state.last_heartbeat_time = clock_t::now();
}

void consensus::on_answer_ok(const cluster_node &from, const append_entries &e) {
  logger_info("node: ", _settings.name(), ": answer from:", from, " cur:", e.current,
              ", prev", e.prev, ", ci:", e.commited);

  // TODO check for current>_last_for_cluster[from];
  if (!e.prev.is_empty()) {
    _log_state[from] = e.prev;
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
      send_all(entries_kind_t::APPEND);
    }
  }
  // replicate_log();
}

void consensus::commit_reccord(const logdb::reccord_info &target) {
  auto to_commit = _jrn->first_uncommited_rec();
  if (!to_commit.is_empty()) {

    auto i = to_commit.lsn;
    while (i <= target.lsn && i <= _jrn->prev_rec().lsn) {
      logger_info("node: ", _settings.name(), ": commit entry lsv:", i);
      _jrn->commit(i++);
      auto commited = _jrn->commited_rec();
      auto le = _jrn->get(commited.lsn);
      _consumer->apply_cmd(le.cmd);
    }
  }
}

void consensus::heartbeat() {
  std::lock_guard<std::mutex> l(_locker);

  if (_state.node_kind != NODE_KIND::LEADER && _state.is_heartbeat_missed()) {
    // logger_info("node: ", _settings.name(), ": heartbeat");
    const auto old_s = _state;
    const auto ns = node_state_t::heartbeat(_state, _self_addr, _cluster->size());
    _state = ns;

    logger_info("node: ", _settings.name(), ":heartbeat. ", _state.node_kind, "=> ",
                old_s.node_kind, " leader:", _state.leader);
  }

  if (_state.node_kind == NODE_KIND::CANDIDATE || _state.node_kind == NODE_KIND::LEADER) {

    if (_state.node_kind == NODE_KIND::LEADER) { /// CANDIDATE => LEADER
      if (_log_state.find(_self_addr) == _log_state.end()) {
        _log_state[_self_addr] = _jrn->prev_rec();
      }
      replicate_log();
    } else { /// CANDIDATE => CANDIDATE
      _log_state.clear();
      _log_state[_self_addr] = _jrn->prev_rec();
      _last_sended.clear();
      auto ae = make_append_entries();
      ae.kind = entries_kind_t::VOTE;
      logger_info("node: ", _settings.name(), ":send all vote. ");
      _cluster->send_all(_self_addr, ae);
    }
  }
  update_next_heartbeat_interval();
}

void consensus::replicate_log() {
  logger_info("node: ", _settings.name(), ": log replication");
  // TODO check if log is empty.
  auto self_log_state = _log_state[_self_addr];
  auto all = _cluster->all_nodes();
  /*auto jrn_sz = _jrn->size();
  auto jrn_is_empty = jrn_sz == size_t(0);*/
  for (const auto &naddr : all) {
    if (naddr == _self_addr) {
      continue;
    }
    auto kv = _log_state.find(naddr);
    if (kv == _log_state.end()) {
      send(naddr, entries_kind_t::HEARTBEAT);
      continue;
    }
    bool is_append = false;
    if (kv->second.is_empty()
        || /*!self_log_state.is_empty() && */ kv->second.lsn < self_log_state.lsn) {
      logger_info("node: ", _settings.name(), ": check replication for ", kv->first,
                  " => lsn:", kv->second.lsn, " < self.lsn:", self_log_state.lsn,
                  "==", kv->second.lsn < self_log_state.lsn);

      auto lsn_to_replicate = kv->second.lsn;
      if (kv->second.is_empty()) {
        lsn_to_replicate = _jrn->first_rec().lsn;
      } else {
        ++lsn_to_replicate; // we need a next record;
      }
      if (_last_sended[kv->first].lsn != lsn_to_replicate) {
        auto ae = make_append_entries(rft::entries_kind_t::APPEND);
        auto cur = _jrn->get(lsn_to_replicate);
        ae.current.lsn = lsn_to_replicate;
        ae.current.term = cur.term;
        ae.cmd = cur.cmd;
        if (!kv->second.is_empty()) {
          auto prev = _jrn->get(kv->second.lsn);
          ae.prev.lsn = kv->second.lsn;
          ae.prev.term = kv->second.term;
        }
        _last_sended[kv->first] = ae.current;
        logger_info("node: ", _settings.name(), ": replicate cur:", ae.current,
                    " prev:", ae.prev, " to ", kv->first);
        _cluster->send_to(_self_addr, kv->first, ae);
        is_append = true;
      }
    }
    if (!is_append) {
      send(naddr, entries_kind_t::HEARTBEAT);
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

  auto current = _jrn->put(le);
  ENSURE(_jrn->size() != size_t(0));

  _log_state[_self_addr] = current;
  logger_info("node: ", _settings.name(), ": add_command: ", current);
  if (_cluster->size() == size_t(1)) {
    commit_reccord(_jrn->first_uncommited_rec());
  }
}