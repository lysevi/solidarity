#include <librft/consensus.h>
#include <librft/utils/logger.h>

using namespace rft;
using namespace rft::utils::logging;

namespace {
inline std::mt19937 make_seeded_engine() {
  std::random_device r;
  std::seed_seq seed{r(), r(), r(), r(), r(), r(), r(), r()};
  return std::mt19937(seed);
}
} // namespace

consensus::consensus(const node_settings &ns,
                     const std::shared_ptr<abstract_cluster> &cluster,
                     const logdb::journal_ptr &jrn)
    : _settings(ns), _cluster(cluster), _jrn(jrn), _last_heartbeat_time(),
      _rnd_eng(make_seeded_engine()) {

  logger_info("node ", ns.name(),
              ": election_timeout(ms)=", ns.election_timeout().count());

  _self_addr.set_name(_settings.name());

  _start_time = clock_t::now().time_since_epoch().count();
}

append_entries consensus::make_append_entries_unsafe() const {
  append_entries ae;
  ae.round = _round;
  ae.starttime = _start_time;
  ae.leader_term = _leader_term;
  ae.is_vote = false;
  return ae;
}

append_entries consensus::make_append_entries() const {
  std::shared_lock<std::shared_mutex> l(_locker);
  return make_append_entries_unsafe();
}

bool consensus::is_heartbeat_missed() const {
  auto now = clock_t::now();
  auto r = (now - _last_heartbeat_time) > (_next_heartbeat_interval * 2);
  return r;
}

void consensus::change_state(const CONSENSUS_STATE s, const round_t r,
                             const cluster_node &leader) {
  std::lock_guard<std::shared_mutex> l(_locker);
  logger_info("node: ", _settings.name(), ": change state {", _state, ", ", _round, ", ",
              _leader_term, "} => {", s, ", ", r, ", ", leader, "}");
  _state = s;
  _round = r;
  _leader_term = leader;
}

void consensus::change_state(const cluster_node &cn, const round_t r) {
  std::lock_guard<std::shared_mutex> l(_locker);
  logger_info("node: ", _settings.name(), ": change state {", _leader_term, ", ", _round,
              "} => {", cn, ", ", r, "}");
  _leader_term = cn;
  _round = r;
}

void consensus::recv(const cluster_node &from, const append_entries &e) {
  if (e.is_vote) {
    on_vote(from, e);
  } else {
    on_append_entries(from, e);
  }
}

void consensus::on_vote(const cluster_node &from, const append_entries &e) {
  if (e.leader_term != _leader_term) {
    switch (_state) {
    case CONSENSUS_STATE::FOLLOWER: {
      if (_leader_term.is_empty()) {
        _last_heartbeat_time = clock_t::now();
        change_state(e.leader_term, e.round);
        logger_info("node: ", _settings.name(), ": now have a leader - ", _leader_term);

        auto ae = make_append_entries();
        ae.is_vote = true;
        _cluster->send_to(_self_addr, from, ae);
      } else {
        if (_round < e.round) {
          change_state(e.leader_term, e.round);
        } else {
        }
        auto ae = make_append_entries();
        ae.is_vote = true;
        _cluster->send_to(_self_addr, from, ae);
      }
      break;
    }
    case CONSENSUS_STATE::LEADER: {
      // TODO if round != current
      if (_round < e.round) {
        change_state(CONSENSUS_STATE::FOLLOWER, e.round, e.leader_term);
      } else {
        auto ae = make_append_entries();
        ae.is_vote = true;
        _cluster->send_to(_self_addr, from, ae);
      }
      break;
    }
    case CONSENSUS_STATE::CANDIDATE: {
      if (_cluster->size() == size_t(2)) {
        /// sender.uptime > self.uptime => sender is a leader
        if (e.starttime < _start_time) {
          change_state(CONSENSUS_STATE::FOLLOWER, e.round, e.leader_term);
          logger_info("node: ", _settings.name(), ": ", CONSENSUS_STATE::CANDIDATE,
                      " => ", _state);
        }
      } else {
        if (_round < e.round) {
          change_state(CONSENSUS_STATE::FOLLOWER, e.round, e.leader_term);
        } else {
          auto ae = make_append_entries();
          ae.is_vote = true;
          _cluster->send_to(_self_addr, from, ae);
        }
      }
      break;
    }
    }

  } else {
    switch (_state) {
    case CONSENSUS_STATE::FOLLOWER: {
      _last_heartbeat_time = clock_t::now();
      break;
    }
    case CONSENSUS_STATE::CANDIDATE: {
      // TODO use map. node may send one message twice.
      logger_info("node: ", _settings.name(), ": recv. vote from ", from);
      _election_to_me.fetch_add(1);
      auto quorum = (size_t(_cluster->size() / 2.0) + 1);
      if (_election_to_me.load() >= quorum) {
        std::lock_guard<std::shared_mutex> l(_locker);
        _round++;
        _state = CONSENSUS_STATE::LEADER;
        logger_info("node: ", _settings.name(), ": quorum. i'am new leader with ",
                    _election_to_me.load(), " voices");
        _cluster->send_all(_self_addr, make_append_entries_unsafe());
      }
      break;
    }
    }
  }
}

void consensus::on_append_entries(const cluster_node &from, const append_entries &e) {
  switch (_state) {
  case CONSENSUS_STATE::FOLLOWER: {
    std::lock_guard<std::shared_mutex> l(_locker);
    _last_heartbeat_time = clock_t::now();
    if (_leader_term.is_empty()) {
      _leader_term = e.leader_term;
      _round = e.round;
    }
    break;
  }
  case CONSENSUS_STATE::LEADER: {
    std::lock_guard<std::shared_mutex> l(_locker);
    if (_round < e.round) {
      _state = CONSENSUS_STATE::FOLLOWER;
      _round = e.round;
      _leader_term = e.leader_term;
      // TODO log replication
    }
    break;
  }
  case CONSENSUS_STATE::CANDIDATE: {
    std::lock_guard<std::shared_mutex> l(_locker);
    if (_round < e.round) {
      _state = CONSENSUS_STATE::FOLLOWER;
      _round = e.round;
      _leader_term = e.leader_term;
      _election_to_me.store(0);
    }
    break;
  }
  }
}

void consensus::on_heartbeat() {
  std::lock_guard<std::shared_mutex> l(_locker);
  logger_info("node: ", _settings.name(), ": heartbeat");
  if (is_heartbeat_missed() && _state != CONSENSUS_STATE::LEADER) {
    _leader_term.clear();
    switch (_state) {
    case CONSENSUS_STATE::FOLLOWER: {
      if (_cluster->size() == size_t(1)) {
        _round++;
        _leader_term = _self_addr;
        _state = CONSENSUS_STATE::LEADER;
        logger_info("node: ", _settings.name(), ": alone node. change state to ", _state);
      } else {
        _state = CONSENSUS_STATE::CANDIDATE;
        _round++;
        _election_to_me.store(1);
        _leader_term = _self_addr;
        logger_info("node: ", _settings.name(), ": change state to ", _state);
        append_entries ae = make_append_entries_unsafe();
        ae.is_vote = true;
        _cluster->send_all(_self_addr, ae);
      }
      break;
    }
    case CONSENSUS_STATE::CANDIDATE:
      _round++;
      _election_to_me.store(0);
      logger_info("node: ", _settings.name(), ": change state to ", _state,
                  " leader is the ", _leader_term);
      auto ae = make_append_entries_unsafe();
      ae.is_vote = true;
      _cluster->send_all(_self_addr, ae);

      break;
    }

  } else {
    switch (_state) {
    case CONSENSUS_STATE::LEADER: {
      auto ae = make_append_entries_unsafe();
      _cluster->send_all(_self_addr, ae);
      break;
    }
    }
  }
  auto total_mls = _settings.election_timeout().count();
  std::uniform_int_distribution<uint64_t> distr(uint64_t(total_mls / 2.0), total_mls);

  _next_heartbeat_interval = std::chrono::milliseconds(distr(_rnd_eng));
  logger_info("node: ", _settings.name(), ": next heartbeat is ",
              _next_heartbeat_interval.count());
}