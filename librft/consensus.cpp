#include <librft/consensus.h>
#include <libutils/logger.h>
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
                     const logdb::journal_ptr &jrn)
    : _settings(ns)
    , _cluster(cluster)
    , _jrn(jrn)
    , _rnd_eng(make_seeded_engine()) {

  logger_info("node ", ns.name(),
              ": election_timeout(ms)=", ns.election_timeout().count());

  _self_addr.set_name(_settings.name());

  _state.start_time = clock_t::now().time_since_epoch().count();
  update_next_heartbeat_interval();
  _state.last_heartbeat_time = clock_t::now();
}

append_entries consensus::make_append_entries_unsafe() const {
  append_entries ae;
  ae.round = _state.round;
  ae.starttime = _state.start_time;
  ae.leader = _state.leader;
  ae.is_vote = false;
  return ae;
}

append_entries consensus::make_append_entries() const {
  return make_append_entries_unsafe();
}

void consensus::change_state(const ROUND_KIND s,
                             const round_t r,
                             const cluster_node &leader) {
  auto old_state = _state;
  _state.round_kind = s;
  _state.round = r;
  _state.leader = leader;
  logger_info("node: ", _settings.name(), ": change state ", old_state, " => ", _state);
}

void consensus::change_state(const cluster_node &leader, const round_t r) {
  auto old_state = _state;
  _state.round = r;
  _state.leader = leader;
  logger_info("node: ", _settings.name(), ": change state ", old_state, " => ", _state);
}

void consensus::recv(const cluster_node &from, const append_entries &e) {
  std::lock_guard<std::mutex> l(_locker);
  if (e.round < _state.round) {
    return;
  }
  if (e.is_vote) {
    on_vote(from, e);
  } else {
    on_append_entries(from, e);
  }
}

void consensus::on_vote(const cluster_node &from, const append_entries &e) {
  auto old_s = _state;
  changed_state_t change_state_v =
      node_state_t::on_vote(_state, _self_addr, _cluster->size(), from, e);

  node_state_t ns = change_state_v.new_state;
  _state = ns;
  if (_state.round_kind != old_s.round_kind) {
    if (old_s.round_kind == ROUND_KIND::CANDIDATE &&
        ns.round_kind == ROUND_KIND::LEADER) { // if CANDIDATE => LEADER
      std::stringstream ss;
      for (auto v : ns.votes_to_me) {
        ss << v.name() << ", ";
      }
      logger_info("node: ", _settings.name(), ": quorum. i'am new leader with ",
                  ns.votes_to_me.size(), " voices - ", ss.str());
      _state.votes_to_me.clear();
    } else {
      logger_info("node: ", _settings.name(), ": change state ", old_s, " => ", _state);
    }
  }
  switch (change_state_v.notify) {
  case NOTIFY_TARGET::ALL: {
    auto ae = make_append_entries_unsafe();
    ae.is_vote = true;
    _cluster->send_all(_self_addr, ae);
    break;
  }
  case NOTIFY_TARGET::SENDER: {
    auto ae = make_append_entries_unsafe();
    ae.is_vote = true;
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

void consensus::on_append_entries(const cluster_node &from, const append_entries &e) {
  _state = node_state_t::on_append_entries(_state, from, _jrn.get(), e);
}

void consensus::on_heartbeat() {
  std::lock_guard<std::mutex> l(_locker);

  if (_state.round_kind != ROUND_KIND::LEADER && _state.is_heartbeat_missed()) {
    logger_info("node: ", _settings.name(), ": heartbeat");
    if (_state.round_kind == ROUND_KIND::ELECTION) {
      logger_info(1);
    }
    auto old_s = _state;
    auto ns = node_state_t::on_heartbeat(_state, _self_addr, _cluster->size());
    _state = ns;

    logger_info("node: ", _settings.name(), ": {", _state.round_kind, "} => - ",
                _state.leader);
  }

  if (_state.round_kind == ROUND_KIND::CANDIDATE ||
      _state.round_kind == ROUND_KIND::LEADER) {
    if (_state.round_kind == ROUND_KIND::LEADER) {
      logger_info("node: ", _settings.name(), ": heartbeat");
    }
    auto ae = make_append_entries_unsafe();
    ae.is_vote = _state.round_kind == ROUND_KIND::CANDIDATE;
    _cluster->send_all(_self_addr, ae);
  }
  update_next_heartbeat_interval();
}

void consensus::update_next_heartbeat_interval() {
  auto total_mls = _settings.election_timeout().count();
  double k1 = 0.5, k2 = 2.0;
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
  logger_info("node: ", _settings.name(), ": next heartbeat is ",
              _state.next_heartbeat_interval.count());
}