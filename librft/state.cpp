#include <librft/state.h>
#include <libutils/logger.h>
#include <libutils/utils.h>
#include <cmath>
#include <sstream>

using namespace rft;
using namespace utils::logging;

std::string rft::to_string(const node_state_t &s) {
  std::stringstream ss;
  ss << "{ K:" << to_string(s.node_kind) << ", N:" << s.term
     << ", L:" << to_string(s.leader);
  if (s.node_kind == NODE_KIND::CANDIDATE) {
    ss << ", E:" << s.election_round;
  }
  ss << "}";
  return ss.str();
}

void node_state_t::change_state(const NODE_KIND s,
                                const term_t r,
                                const node_name &leader_) {
  node_kind = s;
  term = r;
  leader = leader_;
}

void node_state_t::change_state(const node_name &leader_, const term_t r) {
  term = r;
  leader = leader_;
}

bool node_state_t::is_my_jrn_biggest(const node_state_t &self,
                                     const logdb::reccord_info commited,
                                     const append_entries &e) {
  return self.term < e.term
         || (self.term == e.term && commited.lsn <= e.commited.lsn
             && !commited.lsn_is_empty() && !e.commited.lsn_is_empty());
}

changed_state_t node_state_t::on_vote(const node_state_t &self,
                                      const node_settings &settings,
                                      const node_name &self_addr,
                                      const logdb::reccord_info commited,
                                      const size_t cluster_size,
                                      const node_name &from,
                                      const append_entries &e) {
  node_state_t result = self;
  NOTIFY_TARGET target = NOTIFY_TARGET::NOBODY;

  if (e.leader != result.leader) {
    switch (result.node_kind) {
    case NODE_KIND::ELECTION: {
      if (result.leader.is_empty()) {
        // result.last_heartbeat_time = clock_t::now();
        result.leader = e.leader;
        result.term = e.term;
        target = NOTIFY_TARGET::SENDER;
      }

      break;
    }
    case NODE_KIND::FOLLOWER: {
      // vote to biggest journal.
      if (is_my_jrn_biggest(result, commited, e)) {
        result.node_kind = NODE_KIND::ELECTION;
        result.term = e.term;
        result.leader = e.leader;
        result.last_heartbeat_time = clock_t::now();
      }
      target = NOTIFY_TARGET::SENDER;
      break;
    }
    case NODE_KIND::LEADER: {
      if (result.term < e.term) {
        result.change_state(NODE_KIND::ELECTION, e.term, e.leader);
        // result.last_heartbeat_time = clock_t::now();
      }
      target = NOTIFY_TARGET::SENDER;

      break;
    }
    case NODE_KIND::CANDIDATE: {
      // vote to biggest journal.
      if (is_my_jrn_biggest(result, commited, e)) {
        result.node_kind = NODE_KIND::ELECTION;
        result.term = e.term;
        result.leader = e.leader;
        result.last_heartbeat_time = clock_t::now();
      }
      target = NOTIFY_TARGET::SENDER;
      break;
    }
    }
  } else {
    switch (result.node_kind) {
    case NODE_KIND::LEADER:
      break;
    case NODE_KIND::ELECTION: {
      // TODO ??
      // result.last_heartbeat_time = clock_t::now();
      result.term = e.term;
      target = NOTIFY_TARGET::SENDER;
      break;
    }
    case NODE_KIND::FOLLOWER: {
      // result.last_heartbeat_time = clock_t::now();
      target = NOTIFY_TARGET::SENDER;
      break;
    }
    case NODE_KIND::CANDIDATE: {
      result.votes_to_me.insert(from);
      size_t quorum = quorum_for_cluster(cluster_size, settings.vote_quorum());
      ENSURE(quorum <= cluster_size);
      if (result.votes_to_me.size() >= quorum) {
        result.node_kind = NODE_KIND::LEADER;
        result.term++;
        result.election_round = 0;
        result.leader = self_addr;
        target = NOTIFY_TARGET::ALL;
      } else {
        target = NOTIFY_TARGET::NOBODY;
      }
      break;
    }
    }
  }
  return changed_state_t{result, target};
}

node_state_t node_state_t::on_append_entries(const node_state_t &self,
                                             const node_name &from,
                                             const logdb::abstract_journal *jrn,
                                             const append_entries &e) {
  node_state_t result = self;
  switch (result.node_kind) {
  case NODE_KIND::ELECTION: {
    if (from == result.leader || (from == e.leader && e.term >= result.term)) {
      result.change_state(NODE_KIND::FOLLOWER, e.term, from);
      result.leader = e.leader;
      result.last_heartbeat_time = clock_t::now();
    } else {
      // TODO send error to 'from';
    }
    break;
  }
  case NODE_KIND::FOLLOWER: {
    if (/*self.leader.is_empty() ||*/ e.term > result.term) {
      result.leader = e.leader;
      result.term = e.term;
      // result.last_heartbeat_time = clock_t::now();
    }
    break;
  }
  case NODE_KIND::LEADER: {
    auto last_lst = jrn->commited_rec();
    if (result.term < e.term || (e.commited.term > last_lst.term)
        || (e.commited.term != logdb::UNDEFINED_TERM
            && last_lst.term == logdb::UNDEFINED_TERM)) {
      result.node_kind = NODE_KIND::FOLLOWER;
      result.term = e.term;
      result.leader = e.leader;
      // TODO log replication
    }
    break;
  }
  case NODE_KIND::CANDIDATE: {
    if (result.term <= e.term && e.leader == from) {
      result.node_kind = NODE_KIND::FOLLOWER;
      result.election_round = 0;
      result.term = e.term;
      result.leader = e.leader;
      result.votes_to_me.clear();
    }
    break;
  }
  }
  return result;
}

node_state_t node_state_t::heartbeat(const node_state_t &self,
                                     const node_name &self_addr,
                                     const size_t cluster_size) {
  node_state_t result = self;
  if (result.node_kind != NODE_KIND::LEADER && result.is_heartbeat_missed()) {
    result.leader.clear();
    switch (result.node_kind) {
    case NODE_KIND::LEADER:
      break;
    case NODE_KIND::ELECTION: {
      result.node_kind = NODE_KIND::CANDIDATE;
      result.leader = self_addr;
      result.term += 1;
      result.election_round = 1;
      break;
    }
    case NODE_KIND::FOLLOWER: {
      if (cluster_size == size_t(1)) {
        result.term++;
        result.leader = self_addr;
        result.node_kind = NODE_KIND::LEADER;
      } else {
        result.node_kind = NODE_KIND::CANDIDATE;
        result.term++;
        result.leader = self_addr;
        result.election_round = 1;
        result.votes_to_me.insert(self_addr);
      }
      break;
    }
    case NODE_KIND::CANDIDATE:
      result.leader = self_addr;
      result.term++;
      if (result.election_round < 5) {
        result.election_round++;
      }
      result.votes_to_me.clear();
      result.votes_to_me.insert(self_addr);
      break;
    }
  }
  return result;
}