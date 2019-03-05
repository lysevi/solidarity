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

void consensus::update_next_heartbeat_interval() {
  const auto total_mls = _settings.election_timeout().count();
  double k1 = 0.5, k2 = 2.0;
  if (_state.round_kind == ROUND_KIND::ELECTION) {
    k1 = 0.5;
    k2 = 1.0;
  }
  if (_state.round_kind == ROUND_KIND::CANDIDATE) {
    k1 = 2.0;
    k2 = 3.0 * _state.election_round;
  }
  if (_state.round_kind == ROUND_KIND::LEADER) {
    k1 = 0.5;
    k2 = 1.5;
  }
  std::uniform_int_distribution<uint64_t> distr(uint64_t(total_mls * k1),
                                                uint64_t(total_mls * k2));

  _state.next_heartbeat_interval = std::chrono::milliseconds(distr(_rnd_eng));
  /*logger_info("node: ", _settings.name(), ": next heartbeat is ",
              _state.next_heartbeat_interval.count());*/
}

append_entries consensus::make_append_entries(const entries_kind_t kind) const noexcept {
  append_entries ae;
  ae.round = _state.round;
  ae.starttime = _state.start_time;
  ae.leader = _state.leader;
  ae.kind = kind;
  ae.prev = _jrn->prev_rec();
  ae.commited = _jrn->commited_rec();
  return ae;
}

void consensus::on_vote(const cluster_node &from, const append_entries &e) {
  const auto old_s = _state;
  const changed_state_t change_state_v
      = node_state_t::on_vote(_state, _settings, _self_addr, _cluster->size(), from, e);

  const node_state_t ns = change_state_v.new_state;
  _state = ns;
  if (_state.round_kind != old_s.round_kind) {
    if (old_s.round_kind == ROUND_KIND::CANDIDATE
        && ns.round_kind == ROUND_KIND::LEADER) { // if CANDIDATE => LEADER
      std::stringstream ss;
      auto sz = ns.votes_to_me.size();
      for (auto &&v : ns.votes_to_me) {
        ss << v.name() << ", ";
      }
      logger_info("node: ", _settings.name(), ": quorum. i'am new leader with ", sz,
                  " voices - ", ss.str());
      _state.votes_to_me.clear();
    } else {
      logger_info("node: ", _settings.name(), ": change state ", old_s, " => ", _state);
    }
  }

  switch (change_state_v.notify) {
  case NOTIFY_TARGET::ALL: {
    const auto ae = make_append_entries(entries_kind_t::VOTE);
    _cluster->send_all(_self_addr, ae);
    break;
  }
  case NOTIFY_TARGET::SENDER: {
    const auto ae = make_append_entries(entries_kind_t::VOTE);
    _cluster->send_to(_self_addr, from, ae);
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
  if (e.round < _state.round) {
    return;
  }
  switch (e.kind) {
  case entries_kind_t::VOTE: {
    on_vote(from, e);
    _last_for_cluster[from] = e.current;
    break;
  }
  case entries_kind_t::APPEND: {
    on_append_entries(from, e);
    break;
  }
  case entries_kind_t::ANSWER_OK: {
    on_answer(from, e);
    break;
  }
  default:
    NOT_IMPLEMENTED;
  };
}

void consensus::on_append_entries(const cluster_node &from, const append_entries &e) {
  const auto ns = node_state_t::on_append_entries(_state, from, _jrn.get(), e);
  bool is_demotion
      = _state.round_kind == ROUND_KIND::LEADER && ns.round_kind == ROUND_KIND::FOLLOWER;

  if (ns.round != _state.round) {
    _state = ns;
    if (is_demotion) {
      _last_for_cluster.clear();
    }
  } else {

    auto self_prev = _jrn->prev_rec();
    if (!e.cmd.is_empty() && e.prev != self_prev && !self_prev.is_empty()) {
      auto ae = make_append_entries(entries_kind_t::ANSWER_FAILED);
      _cluster->send_to(_self_addr, from, ae);
    } else {

      auto ae = make_append_entries(entries_kind_t::ANSWER_OK);
      // TODO add check prev,cur,commited
      if (!e.cmd.is_empty()) {
        ENSURE(!e.current.is_empty());
        logger_info("node: ", _settings.name(), ": new entry from ", from, " {",
                    e.current.round, ", ", e.current.lsn, "}");
        if (e.current != _jrn->prev_rec()) {
          logger_info("node: ", _settings.name(), ": write to journal ");
          logdb::log_entry le;
          le.round = _state.round;
          le.cmd = e.cmd;
          ae.current=_jrn->put(le);
        } else {
          logger_info("node: ", _settings.name(), ": duplicates");
        }
      }

      if (!e.commited.is_empty()) { /// commit uncommited
        if (_jrn->commited_rec().lsn != e.commited.lsn) {
          auto to_commit = _jrn->first_uncommited_rec();
          // ENSURE(!to_commit.is_empty());
          if (!to_commit.is_empty()) {

            auto i = to_commit.lsn;
            while (i <= e.commited.lsn) {
              logger_info("node: ", _settings.name(), ": commit entry from ", from,
                          " { lsn:", i, "}");
              _jrn->commit(i++);
              auto commited = _jrn->commited_rec();
			  ae.commited=commited;
              auto le = _jrn->get(commited.lsn);
              _consumer->apply_cmd(le.cmd);
            }
          }
        }
      }

      /// Pong for "heartbeat"

      _cluster->send_to(_self_addr, from, ae);
    }
  }
  _state.last_heartbeat_time = clock_t::now();
}

void consensus::on_answer(const cluster_node &from, const append_entries &e) {
  auto prev = _jrn->prev_rec();
  logger_info("node: ", _settings.name(), ": answer from ", from, " to {", prev.round,
              ", ", prev.lsn, "}");

  // TODO check for current>_last_for_cluster[from];
  if (!e.current.is_empty() && e.current == _jrn->prev_rec()) {
    _last_for_cluster[from] = e.current;
  }

  const auto quorum = _cluster->size() * _settings.append_quorum();

  auto target = _jrn->first_uncommited_rec();

  auto cnt = std::count_if(_last_for_cluster.cbegin(), _last_for_cluster.cend(),
                           [quorum, target](auto &kv) { return kv.second == target; });
  if (size_t(cnt) >= _cluster->size()) {
    logger_info("node: ", _settings.name(), ": append quorum ", cnt, " from ", quorum);
    _jrn->commit(target.lsn);

    auto ae = make_append_entries(entries_kind_t::APPEND);
    _consumer->apply_cmd(_jrn->get(ae.commited.lsn).cmd);

    _cluster->send_all(_self_addr, ae);
  }

  /*for (auto &kv : _last_for_cluster) {
    if (kv.second.lsn >= target.lsn || kv.first == _self_addr) {
      continue;
    } else {
      logdb::log_entry le;
      le.cmd = _jrn->get(kv.second.lsn + 1).cmd;
      le.round = _state.round;

      auto ae = make_append_entries(rft::entries_kind_t::APPEND);
      ae.cmd = le.cmd;

      logger_info("node: ", _settings.name(), ": add_command  {", ae.current.round, ", ",
                  ae.current.lsn, "}");
      _cluster->send_all(_self_addr, ae);
    }
  }*/
}

void consensus::on_heartbeat() {
  std::lock_guard<std::mutex> l(_locker);

  if (_state.round_kind != ROUND_KIND::LEADER && _state.is_heartbeat_missed()) {
    // logger_info("node: ", _settings.name(), ": heartbeat");
    const auto old_s = _state;
    const auto ns = node_state_t::on_heartbeat(_state, _self_addr, _cluster->size());
    _state = ns;

    logger_info("node: ", _settings.name(), ": {", _state.round_kind, "} => - ",
                _state.leader);
  }

  if (_state.round_kind == ROUND_KIND::CANDIDATE
      || _state.round_kind == ROUND_KIND::LEADER) {
    auto ae = make_append_entries();
    if (_state.round_kind == ROUND_KIND::LEADER) { /// CANDIDATE => LEADER
      // logger_info("node: ", _settings.name(), ": heartbeat");
    } else { /// CANDIDATE => CANDIDATE
      _last_for_cluster.clear();
      ae.kind = entries_kind_t::VOTE;
    }
    _cluster->send_all(_self_addr, ae);
  }
  update_next_heartbeat_interval();
}

void consensus::add_command(const command &cmd) {
  // TODO global lock for this method. a while cmd not in consumer;
  std::lock_guard<std::mutex> lg(_locker);
  ENSURE(!cmd.is_empty());
  logdb::log_entry le;
  le.cmd = cmd;
  le.round = _state.round;

  auto ae = make_append_entries(rft::entries_kind_t::APPEND);
  auto current = _jrn->put(le);
  ae.current = current;
  ae.cmd = cmd;

  _last_for_cluster[_self_addr] = current;
  logger_info("node: ", _settings.name(), ": add_command  {", current.round, ", ",
              current.lsn, "}");
  _cluster->send_all(_self_addr, ae);
}