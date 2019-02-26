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

consensus::consensus(const node_settings &ns, abstract_cluster *cluster,
                     const logdb::journal_ptr &jrn)
    : _settings(ns), _cluster(cluster), _jrn(jrn), _last_heartbeat_time(),
      _rnd_eng(make_seeded_engine()) {

  logger_info("node ", ns.name(),
              ": election_timeout(ms)=", ns.election_timeout().count());

  _self_addr.set_name(_settings.name());

  _start_time = clock_t::now().time_since_epoch().count();
  update_next_heartbeat_interval();
  _last_heartbeat_time = clock_t::now();
}

append_entries consensus::make_append_entries_unsafe() const {
  append_entries ae;
  ae.round = _round;
  ae.starttime = _start_time;
  ae.leader = _leader;
  ae.is_vote = false;
  return ae;
}

append_entries consensus::make_append_entries() const {
  return make_append_entries_unsafe();
}

bool consensus::is_heartbeat_missed() const {
  auto now = clock_t::now();
  auto diff =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - _last_heartbeat_time);
  auto r = diff > _next_heartbeat_interval;
  if (r) {
    return true;
  } else {
    return false;
  }
}

void consensus::change_state(const CONSENSUS_STATE s, const round_t r,
                             const cluster_node &leader) {
  logger_info("node: ", _settings.name(), ": change state {", _state, ", ", _round, ", ",
              _leader, "} => {", s, ", ", r, ", ", leader, "}");
  _state = s;
  _round = r;
  _leader = leader;
}

void consensus::change_state(const cluster_node &cn, const round_t r) {
  logger_info("node: ", _settings.name(), ": change state {", _leader, ", ", _round,
              "} => {", cn, ", ", r, "}");
  _leader = cn;
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
  std::lock_guard<std::mutex> l(_locker);
  if (_round > e.round) {
    return;
  }
  if (e.leader != _self_addr && (e.leader != _leader || _leader.is_empty())) {
    switch (_state) {
    case CONSENSUS_STATE::ELECTION: {
      _last_heartbeat_time = clock_t::now();
      _leader = e.leader;
      _round = e.round;
      logger_info("node: ", _settings.name(), ": vote to - ", from);

      auto ae = make_append_entries();
      ae.is_vote = true;
      _cluster->send_to(_self_addr, from, ae);
      break;
    }
    case CONSENSUS_STATE::FOLLOWER: {
      if (_leader.is_empty()) {
        _leader = e.leader;
      }
      auto ae = make_append_entries();
      ae.is_vote = true;
      _cluster->send_to(_self_addr, from, ae);
      break;
    }
    case CONSENSUS_STATE::LEADER: {
      if (_round < e.round) {
        change_state(CONSENSUS_STATE::FOLLOWER, e.round, e.leader);
        // TODO log replication
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
          _last_heartbeat_time = clock_t::now();
          _election_round = 0;
          change_state(CONSENSUS_STATE::FOLLOWER, e.round, e.leader);
          logger_info("node: ", _settings.name(), ": ", CONSENSUS_STATE::CANDIDATE,
                      " => ", _state);
        }
      } else {
        if (_round < e.round && from == e.leader) {
          change_state(CONSENSUS_STATE::ELECTION, e.round, e.leader);
          _election_round = 0;
          _last_heartbeat_time = clock_t::now();
          _leader = e.leader;
          _round = e.round;

          auto ae = make_append_entries();
          ae.is_vote = true;
          _cluster->send_to(_self_addr, from, ae);
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
    case CONSENSUS_STATE::ELECTION: {
      _last_heartbeat_time = clock_t::now();
      break;
    }
    case CONSENSUS_STATE::FOLLOWER: {
      _last_heartbeat_time = clock_t::now();
      break;
    }
    case CONSENSUS_STATE::CANDIDATE: {
      // TODO use map. node may send one message twice.
      logger_info("node: ", _settings.name(), ": recv. vote from ", from);
      _election_to_me.insert(from);
      auto quorum = (size_t(_cluster->size() / 2.0) + 1);
      if (_election_to_me.size() >= quorum) {
        _state = CONSENSUS_STATE::LEADER;
        _round++;
        _election_round = 0;
        _leader = _self_addr;

        std::stringstream ss;
        for (auto v : _election_to_me) {
          ss << v.name() << ", ";
        }
        logger_info("node: ", _settings.name(), ": quorum. i'am new leader with ",
                    ss.str(), " voices");
        _cluster->send_all(_self_addr, make_append_entries_unsafe());
      }
      break;
    }
    }
  }
}

void consensus::on_append_entries(const cluster_node &from, const append_entries &e) {
  std::lock_guard<std::mutex> l(_locker);
  if (e.round < _round) {
    return;
  }

  switch (_state) {
  case CONSENSUS_STATE::ELECTION: {
    if (from == _leader) {
      change_state(CONSENSUS_STATE::ELECTION, e.round, from);
      _last_heartbeat_time = clock_t::now();
    } else {
      // TODO send error to 'from';
    }
    break;
  }
  case CONSENSUS_STATE::FOLLOWER: {
    if (_leader.is_empty()) {
      _leader = e.leader;
      _round = e.round;
      _last_heartbeat_time = clock_t::now();
    }
    break;
  }
  case CONSENSUS_STATE::LEADER: {
    if (_round < e.round) {
      _state = CONSENSUS_STATE::FOLLOWER;
      _round = e.round;
      _leader = e.leader;
      // TODO log replication
    }
    break;
  }
  case CONSENSUS_STATE::CANDIDATE: {
    if (_round < e.round) {
      _election_round = 0;
      _state = CONSENSUS_STATE::FOLLOWER;
      _round = e.round;
      _leader = e.leader;
      _election_to_me.clear();
    }
    break;
  }
  }
}

void consensus::on_heartbeat() {
  std::lock_guard<std::mutex> l(_locker);
  logger_info("node: ", _settings.name(), ": heartbeat");
  if (_state != CONSENSUS_STATE::LEADER && is_heartbeat_missed()) {
    _leader.clear();
    switch (_state) {
    case CONSENSUS_STATE::ELECTION: {
      _leader.clear();
      _state = CONSENSUS_STATE::FOLLOWER;
      break;
    }
    case CONSENSUS_STATE::FOLLOWER: {
      if (_cluster->size() == size_t(1)) {
        _round++;
        _leader = _self_addr;
        _state = CONSENSUS_STATE::LEADER;
        logger_info("node: ", _settings.name(), ": alone node. change state to ", _state);
      } else {
        _state = CONSENSUS_STATE::CANDIDATE;
        _round++;
        _election_round = 1;
        _election_to_me.insert(_self_addr);
        logger_info("node: ", _settings.name(), ": change state to ", _state);
        append_entries ae = make_append_entries_unsafe();
        ae.leader = _self_addr;
        ae.is_vote = true;
        _cluster->send_all(_self_addr, ae);
      }
      break;
    }
    case CONSENSUS_STATE::CANDIDATE:
      _round++;
      if (_election_round < 5) {
        _election_round++;
      }
      _election_to_me.clear();
      logger_info("node: ", _settings.name(), ": change state to ", _state,
                  " election_round:", _election_round);
      auto ae = make_append_entries_unsafe();
      ae.leader = _self_addr;
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
  update_next_heartbeat_interval();
}

void consensus::update_next_heartbeat_interval() {
  auto total_mls = _settings.election_timeout().count();
  double k1 = 0.5, k2 = 2.0;
  if (_state == CONSENSUS_STATE::CANDIDATE) {
    k1 = 2.0;
    k2 = 3.0 * _election_round;
  }
  if (_state == CONSENSUS_STATE::LEADER) {
    k1 = 0.5;
    k2 = 1.5;
  }
  std::uniform_int_distribution<uint64_t> distr(uint64_t(total_mls * k1),
                                                uint64_t(total_mls * k2));

  _next_heartbeat_interval = std::chrono::milliseconds(distr(_rnd_eng));
  logger_info("node: ", _settings.name(), ": next heartbeat is ",
              _next_heartbeat_interval.count());
}