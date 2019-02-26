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
    : _settings(ns), _cluster(cluster), _jrn(jrn), _rnd_eng(make_seeded_engine()) {

  logger_info("node ", ns.name(),
              ": election_timeout(ms)=", ns.election_timeout().count());

  _self_addr.set_name(_settings.name());

  _start_time = clock_t::now().time_since_epoch().count();
  update_next_heartbeat_interval();
  _nodestate.last_heartbeat_time = clock_t::now();
}

append_entries consensus::make_append_entries_unsafe() const {
  append_entries ae;
  ae.round = _nodestate.round;
  ae.starttime = _start_time;
  ae.leader = _nodestate.leader;
  ae.is_vote = false;
  return ae;
}

append_entries consensus::make_append_entries() const {
  return make_append_entries_unsafe();
}

bool consensus::is_heartbeat_missed() const {
  auto now = clock_t::now();
  auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - _nodestate.last_heartbeat_time);
  auto r = diff > _next_heartbeat_interval;
  if (r) {
    return true;
  } else {
    return false;
  }
}

void consensus::change_state(const ROUND_KIND s, const round_t r,
                             const cluster_node &leader) {
  auto old_state = _nodestate;
  _nodestate.round_kind = s;
  _nodestate.round = r;
  _nodestate.leader = leader;
  logger_info("node: ", _settings.name(), ": change state ", old_state, " => ",
              _nodestate);
}

void consensus::change_state(const cluster_node &leader, const round_t r) {
  auto old_state = _nodestate;
  _nodestate.round = r;
  _nodestate.leader = leader;
  logger_info("node: ", _settings.name(), ": change state ", old_state, " => ",
              _nodestate);
}

void consensus::recv(const cluster_node &from, const append_entries &e) {
  std::lock_guard<std::mutex> l(_locker);
  if (e.round < _nodestate.round) {
    return;
  }
  if (e.is_vote) {
    on_vote(from, e);
  } else {
    on_append_entries(from, e);
  }
}

void consensus::on_vote(const cluster_node &from, const append_entries &e) {
  if (e.leader != _nodestate.leader) {
    switch (_nodestate.round_kind) {
    case ROUND_KIND::ELECTION: {
      _nodestate.last_heartbeat_time = clock_t::now();
      _nodestate.leader = e.leader;
      _nodestate.round = e.round;
      logger_info("node: ", _settings.name(), ": vote to - ", from);

      auto ae = make_append_entries();
      ae.is_vote = true;
      _cluster->send_to(_self_addr, from, ae);
      break;
    }
    case ROUND_KIND::FOLLOWER: {
      if (_nodestate.leader.is_empty()) {
        _nodestate.leader = e.leader;
      }
      auto ae = make_append_entries();
      ae.is_vote = true;
      _cluster->send_to(_self_addr, from, ae);
      break;
    }
    case ROUND_KIND::LEADER: {
      if (_nodestate.round < e.round) {
        change_state(ROUND_KIND::FOLLOWER, e.round, e.leader);
        // TODO log replication
      } else {
        auto ae = make_append_entries();
        ae.is_vote = true;
        _cluster->send_to(_self_addr, from, ae);
      }
      break;
    }
    case ROUND_KIND::CANDIDATE: {
      if (_cluster->size() == size_t(2)) {
        /// sender.uptime > self.uptime => sender is a leader
        if (e.starttime < _start_time) {
          _nodestate.last_heartbeat_time = clock_t::now();
          _nodestate._election_round = 0;
          change_state(ROUND_KIND::FOLLOWER, e.round, e.leader);
          logger_info("node: ", _settings.name(), ": ", ROUND_KIND::CANDIDATE, " => ",
                      _nodestate.round_kind);
        }
      } else {
        if (_nodestate.round < e.round && from == e.leader) {
          change_state(ROUND_KIND::ELECTION, e.round, e.leader);
          _nodestate._election_round = 0;
          _nodestate.last_heartbeat_time = clock_t::now();
          _nodestate.leader = e.leader;
          _nodestate.round = e.round;

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
    switch (_nodestate.round_kind) {
    case ROUND_KIND::ELECTION: {
      _nodestate.last_heartbeat_time = clock_t::now();
      break;
    }
    case ROUND_KIND::FOLLOWER: {
      _nodestate.last_heartbeat_time = clock_t::now();
      break;
    }
    case ROUND_KIND::CANDIDATE: {
      // TODO use map. node may send one message twice.
      logger_info("node: ", _settings.name(), ": recv. vote from ", from, ":", e.round);
      _nodestate._election_to_me.insert(from);
      auto quorum = (size_t(_cluster->size() / 2.0) + 1);
      if (_nodestate._election_to_me.size() >= quorum) {
        _nodestate.round_kind = ROUND_KIND::LEADER;
        _nodestate.round++;
        _nodestate._election_round = 0;
        _nodestate.leader = _self_addr;

        std::stringstream ss;
        for (auto v : _nodestate._election_to_me) {
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

  switch (_nodestate.round_kind) {
  case ROUND_KIND::ELECTION: {
    if (from == _nodestate.leader) {
      change_state(ROUND_KIND::ELECTION, e.round, from);
      _nodestate.last_heartbeat_time = clock_t::now();
    } else {
      // TODO send error to 'from';
    }
    break;
  }
  case ROUND_KIND::FOLLOWER: {
    if (_nodestate.leader.is_empty()) {
      _nodestate.leader = e.leader;
      _nodestate.round = e.round;
      _nodestate.last_heartbeat_time = clock_t::now();
    }
    break;
  }
  case ROUND_KIND::LEADER: {
    if (_nodestate.round < e.round) {
      _nodestate.round_kind = ROUND_KIND::FOLLOWER;
      _nodestate.round = e.round;
      _nodestate.leader = e.leader;
      // TODO log replication
    }
    break;
  }
  case ROUND_KIND::CANDIDATE: {
    if (_nodestate.round < e.round) {
      _nodestate._election_round = 0;
      _nodestate.round_kind = ROUND_KIND::FOLLOWER;
      _nodestate.round = e.round;
      _nodestate.leader = e.leader;
      _nodestate._election_to_me.clear();
    }
    break;
  }
  }
}

void consensus::on_heartbeat() {
  std::lock_guard<std::mutex> l(_locker);
  logger_info("node: ", _settings.name(), ": heartbeat");
  if (_nodestate.round_kind != ROUND_KIND::LEADER && is_heartbeat_missed()) {
    _nodestate.leader.clear();
    switch (_nodestate.round_kind) {
    case ROUND_KIND::ELECTION: {
      _nodestate.round_kind = ROUND_KIND::FOLLOWER;
      _nodestate.round++;
      break;
    }
    case ROUND_KIND::FOLLOWER: {
      if (_cluster->size() == size_t(1)) {
        _nodestate.round++;
        _nodestate.leader = _self_addr;
        _nodestate.round_kind = ROUND_KIND::LEADER;
        logger_info("node: ", _settings.name(), ": alone node. change state to ",
                    _nodestate.round_kind);
      } else {
        _nodestate.round_kind = ROUND_KIND::CANDIDATE;
        _nodestate.round++;
        _nodestate.leader = _self_addr;
        _nodestate._election_round = 1;
        _nodestate._election_to_me.insert(_self_addr);
        logger_info("node: ", _settings.name(), ": change state to ",
                    _nodestate.round_kind);
        append_entries ae = make_append_entries_unsafe();
        ae.is_vote = true;
        _cluster->send_all(_self_addr, ae);
      }
      break;
    }
    case ROUND_KIND::CANDIDATE:
      _nodestate.leader = _self_addr;
      _nodestate.round++;
      if (_nodestate._election_round < 5) {
        _nodestate._election_round++;
      }
      _nodestate._election_to_me.clear();
      logger_info("node: ", _settings.name(), ": change state to ", _nodestate.round_kind,
                  " election_round:", _nodestate._election_round);
      auto ae = make_append_entries_unsafe();
      ae.is_vote = true;
      _cluster->send_all(_self_addr, ae);

      break;
    }

  } else {
    switch (_nodestate.round_kind) {
    case ROUND_KIND::LEADER: {
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
  if (_nodestate.round_kind == ROUND_KIND::CANDIDATE) {
    k1 = 2.0;
    k2 = 3.0 * _nodestate._election_round;
  }
  if (_nodestate.round_kind == ROUND_KIND::LEADER) {
    k1 = 0.5;
    k2 = 1.5;
  }
  std::uniform_int_distribution<uint64_t> distr(uint64_t(total_mls * k1),
                                                uint64_t(total_mls * k2));

  _next_heartbeat_interval = std::chrono::milliseconds(distr(_rnd_eng));
  logger_info("node: ", _settings.name(), ": next heartbeat is ",
              _next_heartbeat_interval.count());
}