#include <librft/consensus.h>
#include <librft/utils/logger.h>

using namespace rft;
using namespace rft::utils::logging;

consensus::consensus(const node_settings &ns, const cluster_ptr &cluster,
                     const logdb::journal_ptr &jrn)
    : _settings(ns), _cluster(cluster), _jrn(jrn), _heartbeat_time() {
  logger_info("node: ", ns.name(),
              " election_timeout(ms):", ns.election_timeout().count());

  _self_addr.set_name(_settings.name());

  _start_time = clock_t::now().time_since_epoch().count();
}

append_entries consensus::make_append_entries() const {
  append_entries ae;
  ae.round = _round;
  ae.starttime = _start_time;
  ae.leader_term = _leader_term;
  return ae;
}

bool consensus::is_heartbeat_missed() const {
  auto now = clock_t::now();
  auto r = (now - _heartbeat_time) > _settings.election_timeout();
  return r;
}

void consensus::recv(const cluster_node &from, const append_entries &e) {
  if (!_leader_term.is_empty() && e.leader_term != _leader_term) {
    switch (_state) {
    case CONSENSUS_STATE::LEADER: {
      append_entries ae = make_append_entries();
      _cluster->send_to(_self_addr, from, ae);
      break;
    }
    case CONSENSUS_STATE::CANDIDATE: {
      if (_cluster->size() == size_t(2)) {
        /// sender.uptiem > self.uptime => sender is a leader
        if (e.starttime < _start_time) {
          _leader_term = e.leader_term;
          _state = CONSENSUS_STATE::FOLLOWER;
          _round = e.round;
          logger_info("node: ", _settings.name(), ": ", CONSENSUS_STATE::CANDIDATE,
                      " => ", _state);
          auto ae = make_append_entries();
          _cluster->send_to(_self_addr, from, ae);
        }
      }
      break;
    }
    }
  } else {
    switch (_state) {
    case CONSENSUS_STATE::FOLLOWER: {
      _heartbeat_time = clock_t::now();
      if (_leader_term.is_empty()) {
        _leader_term = e.leader_term;
        logger_info("node: ", _settings.name(), ": now have a leader - ", _leader_term);
      };
      break;
    }
    }
  }
}

void consensus::on_heartbeat() {
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
        _leader_term = _self_addr;
        append_entries ae = make_append_entries();
        logger_info("node: ", _settings.name(), ": change state to ", _state);
        _cluster->send_all(_self_addr, ae);
      }
      break;
    }
    case CONSENSUS_STATE::CANDIDATE:
      _state = CONSENSUS_STATE::LEADER;
      _leader_term = _self_addr;
      _round++;

      logger_info("node: ", _settings.name(), ": change state to ", _state,
                  " leader is the ", _leader_term);
      break;
    }

  } else {
    switch (_state) {
    case CONSENSUS_STATE::LEADER: {
      auto ae = make_append_entries();
      _cluster->send_all(_self_addr, ae);
      break;
    }
    }
  }
}