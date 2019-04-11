#include "helpers.h"
#include <libsolidarity/raft_state.h>
#include <catch.hpp>

SCENARIO("raft_state_t.vote", "[raft]") {
  solidarity::raft_state_t self;
  solidarity::raft_settings s;
  self.term = 0;
  solidarity::node_name self_addr;
  self_addr.set_name("self_addr");

  solidarity::raft_state_t from_s;
  from_s.term = 1;
  solidarity::node_name from_s_addr;
  from_s_addr.set_name("from_s_addr");

  auto self_ci_rec = solidarity::logdb::reccord_info();

  GIVEN("leader != message.leader") {
    solidarity::append_entries ae;
    ae.leader.set_name(from_s_addr.name());
    ae.term = from_s.term;
    WHEN("self == ELECTION") {
      self.node_kind = solidarity::NODE_KIND::ELECTION;

      WHEN("leader.is_empty") {
        self.leader.clear();
        auto c = solidarity::raft_state_t::on_vote(
            self, s, self_addr, self_ci_rec, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.leader.name(), from_s_addr.name());
          EXPECT_EQ(c.new_state.term, from_s.term);
          EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::SENDER);
        }
      }

      WHEN("!leader.is_empty") {
        self.leader.set_name("some name");
        auto c = solidarity::raft_state_t::on_vote(
            self, s, self_addr, self_ci_rec, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
          EXPECT_EQ(c.new_state.term, self.term);
          EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::NOBODY);
        }
      }
    }
    WHEN("self == FOLLOWER") {
      self.node_kind = solidarity::NODE_KIND::FOLLOWER;
      /*WHEN("leader.term>self.term") {
        self.term = 0;
        auto c = solidarity::raft_state_t::on_vote(self, s, self_addr, self_ci_rec, 2,
                                            from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.leader.name(), from_s_addr.name());
          EXPECT_EQ(c.new_state.term, from_s.term);
          EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::SENDER);
        }
      }*/
      WHEN("leader.commit>=self.commit") {
        solidarity::logdb::reccord_info ci_rec;
        ci_rec.lsn = 1;
        ci_rec.term = 1;
        ae.commited.lsn = 2;
        ae.commited.term = 2;
        auto c
            = solidarity::raft_state_t::on_vote(self, s, self_addr, ci_rec, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.leader.name(), from_s_addr.name());
          EXPECT_EQ(c.new_state.term, from_s.term);
          EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::SENDER);
        }
      }

      WHEN("leader.commit==self.commit || self.lsn<other.lsn") {
        solidarity::logdb::reccord_info ci_rec;
        ci_rec.lsn = 2;
        ci_rec.term = 2;
        self.term = ae.term = 3;
        ae.commited.lsn = 3;
        auto c
            = solidarity::raft_state_t::on_vote(self, s, self_addr, ci_rec, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.leader.name(), from_s_addr.name());
          EXPECT_EQ(c.new_state.term, ae.term);
          EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::SENDER);
        }
      }

      WHEN("leader.commit.is_empty() && !self.commit.is_empty()") {
        solidarity::logdb::reccord_info ci_rec;
        ci_rec.lsn = solidarity::logdb::UNDEFINED_INDEX;
        ci_rec.term = 2;
        ae.commited.lsn = solidarity::logdb::UNDEFINED_INDEX;
        ae.commited.term = 2;
        auto c
            = solidarity::raft_state_t::on_vote(self, s, self_addr, ci_rec, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.leader.name(), from_s_addr.name());
          EXPECT_EQ(c.new_state.term, from_s.term);
          EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::SENDER);
        }
      }
      WHEN("!leader.commit.is_empty() && self.commit.is_empty()") {
        solidarity::logdb::reccord_info ci_rec;
        ci_rec.lsn = 1;
        ci_rec.term = 2;
        ae.commited.lsn = solidarity::logdb::UNDEFINED_INDEX;
        ae.commited.term = 2;
        auto c
            = solidarity::raft_state_t::on_vote(self, s, self_addr, ci_rec, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.leader.name(), from_s_addr.name());
          EXPECT_EQ(c.new_state.term, from_s.term);
          EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::SENDER);
        }
      }
      WHEN("leader.commit.is_empty() && !self.commit.is_empty()") {
        solidarity::logdb::reccord_info ci_rec;
        ci_rec.lsn = solidarity::logdb::UNDEFINED_INDEX;
        ci_rec.term = 2;
        ae.commited.lsn = 3;
        ae.commited.term = 2;
        auto c
            = solidarity::raft_state_t::on_vote(self, s, self_addr, ci_rec, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.leader.name(), from_s_addr.name());
          EXPECT_EQ(c.new_state.term, from_s.term);
          EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::SENDER);
        }
      }
    }

    WHEN("self == CANDIDATE") {
      self.node_kind = solidarity::NODE_KIND::CANDIDATE;
      WHEN("message from newest term") {
        self.leader.set_name(self_addr.name());
        self.term = 0;
        from_s.term = 1;
        auto c = solidarity::raft_state_t::on_vote(
            self, s, self_addr, self_ci_rec, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.node_kind, solidarity::NODE_KIND::ELECTION);
          EXPECT_EQ(c.new_state.leader.name(), from_s_addr.name());
          EXPECT_EQ(c.new_state.term, from_s.term);
          EXPECT_EQ(c.new_state.election_round, size_t(0));
          EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::SENDER);
        }
      }
      WHEN("message from same term") {
        self.leader.set_name(self_addr.name());
        self.term = 1;
        from_s.term = 1;
        auto c = solidarity::raft_state_t::on_vote(
            self, s, self_addr, self_ci_rec, 2, from_s_addr, ae);
        THEN("vote to self.leader") {
          EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
          EXPECT_EQ(c.new_state.term, self.term);
          EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::SENDER);
        }
      }
    }
  }

  GIVEN("leader == message.leader") {
    solidarity::append_entries ae;
    ae.leader.set_name(from_s_addr.name());
    ae.term = from_s.term + 1;

    self.leader.set_name(from_s_addr.name());
    WHEN("self == ELECTION") {
      self.node_kind = solidarity::NODE_KIND::ELECTION;
      self.term = from_s.term;
      auto c = solidarity::raft_state_t::on_vote(
          self, s, self_addr, self_ci_rec, 2, from_s_addr, ae);
      THEN("vote to self.leader") {
        EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
        EXPECT_EQ(c.new_state.term, ae.term);
        EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::SENDER);
      }
    }

    WHEN("self == FOLLOWER") {
      self.node_kind = solidarity::NODE_KIND::FOLLOWER;
      self.term = from_s.term;

      auto c = solidarity::raft_state_t::on_vote(
          self, s, self_addr, self_ci_rec, 2, from_s_addr, ae);
      THEN("vote to self.leader") {
        EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
        EXPECT_EQ(c.new_state.term, self.term);
        EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::SENDER);
      }
    }

    WHEN("self == CANDIDATE") {
      self.node_kind = solidarity::NODE_KIND::CANDIDATE;
      self.election_round = 1;
      ae.leader = self_addr;
      self.leader = self_addr;
      WHEN("quorum") {
        self.votes_to_me.insert(self_addr);
        auto c = solidarity::raft_state_t::on_vote(
            self, s, self_addr, self_ci_rec, 2, from_s_addr, ae);
        THEN("make self a self.leader") {
          EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
          EXPECT_EQ(c.new_state.term, self.term + 1);
          EXPECT_EQ(c.new_state.node_kind, solidarity::NODE_KIND::LEADER);
          EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::ALL);
        }
      }

      WHEN("not a quorum") {
        self.votes_to_me.clear();
        auto c = solidarity::raft_state_t::on_vote(
            self, s, self_addr, self_ci_rec, 3, from_s_addr, ae);
        THEN("wait") {
          EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
          EXPECT_EQ(c.new_state.term, self.term);
          EXPECT_EQ(c.new_state.node_kind, solidarity::NODE_KIND::CANDIDATE);
          EXPECT_EQ(c.notify, solidarity::NOTIFY_TARGET::NOBODY);
        }
      }
    }
  }
}

SCENARIO("raft_state_t.on_append_entries", "[raft]") {
  solidarity::raft_state_t self;
  solidarity::raft_state_t from_s;

  solidarity::node_name self_addr;
  solidarity::node_name from_s_addr;

  from_s.term = 1;
  from_s_addr.set_name("from_s_addr");

  self.term = 1;
  self_addr.set_name("self_addr");
  self.leader = from_s_addr;

  solidarity::append_entries ae;
  ae.leader = from_s_addr;
  ae.term = from_s.term;

  solidarity::logdb::journal_ptr jrn{new solidarity::logdb::memory_journal()};

  WHEN("self == ELECTION") {
    self.node_kind = solidarity::NODE_KIND::ELECTION;

    WHEN("from==self.leader") {
      auto new_state
          = solidarity::raft_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("follow to sender") {
        EXPECT_EQ(new_state.node_kind, solidarity::NODE_KIND::FOLLOWER);
        EXPECT_EQ(new_state.leader.name(), from_s_addr.name());
        EXPECT_EQ(new_state.term, from_s.term);
      }
    }

    WHEN("different term") {
      self.term = 0;
      auto new_state
          = solidarity::raft_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("follow to sender") {
        EXPECT_EQ(new_state.node_kind, solidarity::NODE_KIND::FOLLOWER);
        EXPECT_EQ(new_state.leader.name(), from_s_addr.name());
        EXPECT_EQ(new_state.term, from_s.term);
      }
    }

    WHEN("from==self.leader") {
      self.leader.set_name("other name");
      auto new_state
          = solidarity::raft_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("follow to sender") {
        EXPECT_EQ(new_state.node_kind, solidarity::NODE_KIND::FOLLOWER);
        EXPECT_EQ(new_state.leader.name(), from_s_addr.name());
        EXPECT_EQ(new_state.term, self.term);
      }
    }
  }

  WHEN("self == FOLLOWER") {
    self.node_kind = solidarity::NODE_KIND::FOLLOWER;
    self.leader.clear();
    self.term = 0;
    WHEN("term>self.leader") {
      auto new_state
          = solidarity::raft_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("follow to sender") {
        EXPECT_EQ(new_state.node_kind, solidarity::NODE_KIND::FOLLOWER);
        EXPECT_EQ(new_state.leader.name(), from_s_addr.name());
        EXPECT_EQ(new_state.term, from_s.term);
      }
    }
  }

  WHEN("self == LEADER") {
    self.node_kind = solidarity::NODE_KIND::LEADER;
    WHEN("term>self.term") {
      self.term = 0;
      auto new_state
          = solidarity::raft_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("follow to sender") {
        EXPECT_EQ(new_state.node_kind, solidarity::NODE_KIND::FOLLOWER);
        EXPECT_EQ(new_state.leader.name(), from_s_addr.name());
        EXPECT_EQ(new_state.term, from_s.term);
      }
    }
    WHEN("sender.term>self.term") {
      ae.commited.term++;
      auto new_state
          = solidarity::raft_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("follow to sender") {
        EXPECT_EQ(new_state.node_kind, solidarity::NODE_KIND::FOLLOWER);
        EXPECT_EQ(new_state.leader.name(), from_s_addr.name());
        EXPECT_EQ(new_state.term, from_s.term);
      }
    }
  }

  WHEN("self == CANDIDATE") {
    self.node_kind = solidarity::NODE_KIND::CANDIDATE;

    WHEN("term==self.leader") {
      self.term = from_s.term;
      auto new_state
          = solidarity::raft_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("follow to sender") {
        EXPECT_EQ(new_state.node_kind, solidarity::NODE_KIND::FOLLOWER);
        EXPECT_EQ(new_state.leader.name(), from_s_addr.name());
        EXPECT_EQ(new_state.term, from_s.term);
      }
    }
    WHEN("from==self.leader") {
      self.term = from_s.term + 1;
      auto new_state
          = solidarity::raft_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("do nothing") {
        EXPECT_EQ(new_state.node_kind, self.node_kind);
        EXPECT_EQ(new_state.leader.name(), self.leader.name());
        EXPECT_EQ(new_state.term, self.term);
      }
    }
  }
}

SCENARIO("raft_state_t.on_heartbeat", "[raft]") {
  solidarity::raft_state_t self;
  solidarity::raft_state_t from_s;

  solidarity::node_name self_addr;
  solidarity::node_name from_s_addr;

  from_s.term = 1;
  from_s_addr.set_name("from_s_addr");

  self.term = 1;
  self_addr.set_name("self_addr");
  self.leader = from_s_addr;
  self.last_heartbeat_time = solidarity::clock_t::time_point();
  self.next_heartbeat_interval = std::chrono::milliseconds(0);
  solidarity::append_entries ae;
  ae.leader = from_s_addr;
  ae.term = from_s.term;

  WHEN("self == ELECTION") {
    self.node_kind = solidarity::NODE_KIND::ELECTION;

    auto new_state = solidarity::raft_state_t::heartbeat(self, self_addr, 2);
    THEN("be a follower") {
      EXPECT_EQ(new_state.node_kind, solidarity::NODE_KIND::CANDIDATE);
      EXPECT_EQ(new_state.leader, self_addr);
      EXPECT_EQ(new_state.term, self.term + 1);
      EXPECT_EQ(new_state.election_round, 1);
    }
  }

  WHEN("self == FOLLOWER") {
    self.node_kind = solidarity::NODE_KIND::FOLLOWER;
    WHEN("alone in cluster") {
      auto new_state = solidarity::raft_state_t::heartbeat(self, self_addr, 1);
      THEN("be a LEADER") {
        EXPECT_EQ(new_state.node_kind, solidarity::NODE_KIND::LEADER);
        EXPECT_EQ(new_state.leader.name(), self_addr.name());
        EXPECT_EQ(new_state.term, self.term + 1);
      }
    }

    WHEN("not alone in cluster") {
      auto new_state = solidarity::raft_state_t::heartbeat(self, self_addr, 2);
      THEN("be a LEADER") {
        EXPECT_EQ(new_state.node_kind, solidarity::NODE_KIND::CANDIDATE);
        EXPECT_EQ(new_state.leader.name(), self_addr.name());
        EXPECT_EQ(new_state.election_round, 1);
        EXPECT_EQ(new_state.votes_to_me.size(), size_t(1));
        EXPECT_EQ(new_state.term, self.term + 1);
      }
    }
  }

  WHEN("self == CANDIDATE") {
    self.node_kind = solidarity::NODE_KIND::CANDIDATE;
    WHEN("election_round>=5") {
      self.election_round = 5;
      auto new_state = solidarity::raft_state_t::heartbeat(self, self_addr, 2);
      THEN("be a CANDIDATE") {
        EXPECT_EQ(new_state.node_kind, solidarity::NODE_KIND::CANDIDATE);
        EXPECT_EQ(new_state.leader.name(), self_addr.name());
        EXPECT_EQ(new_state.term, self.term + 1);
        EXPECT_EQ(new_state.election_round, self.election_round);
      }
    }

    WHEN("not alone in cluster") {
      self.election_round = 4;
      auto new_state = solidarity::raft_state_t::heartbeat(self, self_addr, 2);
      THEN("be a CANDIDATE") {
        EXPECT_EQ(new_state.node_kind, solidarity::NODE_KIND::CANDIDATE);
        EXPECT_EQ(new_state.leader.name(), self_addr.name());
        EXPECT_EQ(new_state.term, self.term + 1);
        EXPECT_EQ(new_state.election_round, size_t(5));
      }
    }
  }
}
