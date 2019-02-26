#include <librft/state.h>
#include <libutils/logger.h>

using namespace rft;
using namespace utils::logging;

void node_state_t::change_state(const ROUND_KIND s, const round_t r,
                                const cluster_node &leader_) {
  round_kind = s;
  round = r;
  leader = leader_;
}

void node_state_t::change_state(const cluster_node &leader_, const round_t r) {
  round = r;
  leader = leader_;
}

node_state_t node_state_t::on_vote(const node_state_t &self,
                                   const cluster_node &self_addr,
                                   const size_t cluster_size, const cluster_node &from,
                                   const append_entries &e) {
  node_state_t result = self;
  if (e.leader != result.leader) {
    switch (result.round_kind) {
    case ROUND_KIND::ELECTION: {
      if (result.leader.is_empty()) {
        result.last_heartbeat_time = clock_t::now();
        result.leader = e.leader;
        result.round = e.round;
      }

      break;
    }
    case ROUND_KIND::FOLLOWER: {
      if (result.leader.is_empty()) {
        result.round_kind = ROUND_KIND::ELECTION;
        result.round = e.round;
        result.leader = e.leader;
      }
      break;
    }
    case ROUND_KIND::LEADER: {
      if (result.round < e.round) {

        result.change_state(ROUND_KIND::FOLLOWER, e.round, e.leader);
        // TODO log replication
      }
      break;
    }
    case ROUND_KIND::CANDIDATE: {
      if (result.round < e.round && from == e.leader) {
        result.change_state(ROUND_KIND::ELECTION, e.round, e.leader);
        result.election_round = 0;
        result.last_heartbeat_time = clock_t::now();
      }
      break;
    }
    }
  } else {
    switch (result.round_kind) {
    case ROUND_KIND::ELECTION: {
      result.last_heartbeat_time = clock_t::now();
      result.round = e.round;
      break;
    }
    case ROUND_KIND::FOLLOWER: {
      result.last_heartbeat_time = clock_t::now();
      break;
    }
    case ROUND_KIND::CANDIDATE: {
      result._election_to_me.insert(from);
      auto quorum = (size_t(cluster_size / 2.0) + 1);
      if (result._election_to_me.size() >= quorum) {
        result.round_kind = ROUND_KIND::LEADER;
        result.round++;
        result.election_round = 0;
        result.leader = self_addr;
      }
      break;
    }
    }
  }
  return result;
}

node_state_t node_state_t::on_append_entries(const node_state_t &self,
                                             const cluster_node &from,
                                             const append_entries &e) {
  node_state_t result = self;
  switch (result.round_kind) {
  case ROUND_KIND::ELECTION: {
    if (from == result.leader) {
      result.change_state(ROUND_KIND::FOLLOWER, e.round, from);
	  result.leader=e.leader;
      result.last_heartbeat_time = clock_t::now();
    } else {
      // TODO send error to 'from';
    }
    break;
  }
  case ROUND_KIND::FOLLOWER: {
    if (result.leader.is_empty()) {
      result.leader = e.leader;
      result.round = e.round;
      result.last_heartbeat_time = clock_t::now();
    }
    break;
  }
  case ROUND_KIND::LEADER: {
    if (result.round < e.round) {
      result.round_kind = ROUND_KIND::FOLLOWER;
      result.round = e.round;
      result.leader = e.leader;
      // TODO log replication
    }
    break;
  }
  case ROUND_KIND::CANDIDATE: {
    if (result.round < e.round) {
      result.election_round = 0;
      result.round_kind = ROUND_KIND::FOLLOWER;
      result.round = e.round;
      result.leader = e.leader;
      result._election_to_me.clear();
    }
    break;
  }
  }
  return result;
}

node_state_t node_state_t::on_heartbeat(const node_state_t &self,
                                        const cluster_node &self_addr,
                                        const size_t cluster_size) {
  node_state_t result = self;
  if (result.round_kind != ROUND_KIND::LEADER && result.is_heartbeat_missed()) {
    result.leader.clear();
    switch (result.round_kind) {
    case ROUND_KIND::ELECTION: {
      result.round_kind = ROUND_KIND::FOLLOWER;
      break;
    }
    case ROUND_KIND::FOLLOWER: {
      if (cluster_size == size_t(1)) {
        result.round++;
        result.leader = self_addr;
        result.round_kind = ROUND_KIND::LEADER;
      } else {
        result.round_kind = ROUND_KIND::CANDIDATE;
        result.round++;
        result.leader = self_addr;
        result.election_round = 1;
        result._election_to_me.insert(self_addr);
      }
      break;
    }
    case ROUND_KIND::CANDIDATE:
      result.leader = self_addr;
      result.round++;
      if (result.election_round < 5) {
        result.election_round++;
      }
      result._election_to_me.clear();
      result._election_to_me.insert(self_addr);
      break;
    }
  } 
  return result;
}