#include <librft/consensus.h>
#include <librft/utils/logger.h>

using namespace rft;
using namespace rft::utils::logging;

consensus::consensus(const node_settings &ns, const cluster_ptr &cluster,
                     const logdb::journal_ptr &jrn)
    : _settings(ns), _cluster(cluster), _jrn(jrn) {
  logger_info("node: ", ns.name(),
              " election_timeout(ms):", ns.election_timeout().count());

  _self_addr.set_name(_settings.name());

  _start_time = clock_t::now().time_since_epoch().count();
  _heartbeat_time = clock_t::now();
}

append_entries consensus::make_append_entries() const {
  append_entries ae;
  ae.round = _round;
  ae.starttime = _start_time;
  ae.leader_term = _leader_term;
  return ae;
}

void consensus::recv(const cluster_node &from, const append_entries &e) {
  if (e.leader_term != _self_addr) {
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
          auto ae = make_append_entries();
          _cluster->send_to(_self_addr, from, ae);
        }
      }
      break;
    }
    }
  }
}

void consensus::on_heartbeat() {
  auto now = clock_t::now();
  auto is_heartbeat_missed = (now - _heartbeat_time) > _settings.election_timeout();
  switch (_state) {
  case CONSENSUS_STATE::LEADER:
    break;
  case CONSENSUS_STATE::FOLLOWER: {
    if (_cluster->size() == size_t(1) && is_heartbeat_missed) {
      _round++;
      _leader_term = _self_addr;
      _state = CONSENSUS_STATE::LEADER;
    } else {
      _state = CONSENSUS_STATE::CANDIDATE;
      _leader_term = _self_addr;
      append_entries ae = make_append_entries();

      _cluster->send_all(_self_addr, ae);
    }
    break;
  }
  case CONSENSUS_STATE::CANDIDATE:
    if (is_heartbeat_missed) {
      _state = CONSENSUS_STATE::LEADER;
      _leader_term = _self_addr;
      _round++;
    }
    break;
  }
}