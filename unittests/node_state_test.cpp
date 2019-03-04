#include "helpers.h"
#include <librft/state.h>
#include <catch.hpp>

SCENARIO("node_state.vote") {
  rft::node_state_t self;
  rft::node_settings s;
  self.round = 0;
  rft::cluster_node self_addr;
  self_addr.set_name("self_addr");

  rft::node_state_t from_s;
  from_s.round = 1;
  rft::cluster_node from_s_addr;
  from_s_addr.set_name("from_s_addr");

  GIVEN("leader != message.leader") {
    rft::append_entries ae;
    ae.leader.set_name(from_s_addr.name());
    ae.round = from_s.round;
    WHEN("self == ELECTION") {
      self.round_kind = rft::ROUND_KIND::ELECTION;

      WHEN("leader.is_empty") {
        self.leader.clear();
        auto c = rft::node_state_t::on_vote(self, s, self_addr, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.leader.name(), from_s_addr.name());
          EXPECT_EQ(c.new_state.round, from_s.round);
          EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::SENDER);
        }
      }

      WHEN("!leader.is_empty") {
        self.leader.set_name("some name");
        auto c = rft::node_state_t::on_vote(self, s, self_addr, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
          EXPECT_EQ(c.new_state.round, self.round);
          EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::NOBODY);
        }
      }
    }
    WHEN("self == FOLLOWER") {
      self.round_kind = rft::ROUND_KIND::FOLLOWER;
      WHEN("leader.is_empty") {
        self.leader.clear();
        auto c = rft::node_state_t::on_vote(self, s, self_addr, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.leader.name(), from_s_addr.name());
          EXPECT_EQ(c.new_state.round, from_s.round);
          EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::SENDER);
        }
      }
      WHEN("!leader.is_empty") {
        self.leader.set_name("leader name");
        auto c = rft::node_state_t::on_vote(self, s, self_addr, 2, from_s_addr, ae);
        THEN("vote to self.leader") {
          EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
          EXPECT_EQ(c.new_state.round, self.round);
          EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::SENDER);
        }
      }
    }

    WHEN("self == CANDIDATE") {
      self.round_kind = rft::ROUND_KIND::CANDIDATE;
      WHEN("message from newest round") {
        self.leader.set_name(self_addr.name());
        self.round = 0;
        from_s.round = 1;
        auto c = rft::node_state_t::on_vote(self, s, self_addr, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.round_kind, rft::ROUND_KIND::ELECTION);
          EXPECT_EQ(c.new_state.leader.name(), from_s_addr.name());
          EXPECT_EQ(c.new_state.round, from_s.round);
          EXPECT_EQ(c.new_state.election_round, size_t(0));
          EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::SENDER);
        }
      }
      WHEN("message from same round") {
        self.leader.set_name(self_addr.name());
        self.round = 1;
        from_s.round = 1;
        auto c = rft::node_state_t::on_vote(self, s, self_addr, 2, from_s_addr, ae);
        THEN("vote to self.leader") {
          EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
          EXPECT_EQ(c.new_state.round, self.round);
          EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::SENDER);
        }
      }
    }
  }

  GIVEN("leader == message.leader") {
    rft::append_entries ae;
    ae.leader.set_name(from_s_addr.name());
    ae.round = from_s.round + 1;

    self.leader.set_name(from_s_addr.name());
    WHEN("self == ELECTION") {
      self.round_kind = rft::ROUND_KIND::ELECTION;
      self.round = from_s.round;
      auto c = rft::node_state_t::on_vote(self, s, self_addr, 2, from_s_addr, ae);
      THEN("vote to self.leader") {
        EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
        EXPECT_EQ(c.new_state.round, ae.round);
        EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::SENDER);
      }
    }

    WHEN("self == FOLLOWER") {
      self.round_kind = rft::ROUND_KIND::FOLLOWER;
      self.round = from_s.round;

      auto c = rft::node_state_t::on_vote(self, s, self_addr, 2, from_s_addr, ae);
      THEN("vote to self.leader") {
        EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
        EXPECT_EQ(c.new_state.round, self.round);
        EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::SENDER);
      }
    }

    WHEN("self == CANDIDATE") {
      self.round_kind = rft::ROUND_KIND::CANDIDATE;
      self.election_round = 1;
      ae.leader = self_addr;
      self.leader = self_addr;
      WHEN("quorum") {
        self.votes_to_me.insert(self_addr);
        auto c = rft::node_state_t::on_vote(self, s, self_addr, 2, from_s_addr, ae);
        THEN("make self a self.leader") {
          EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
          EXPECT_EQ(c.new_state.round, self.round + 1);
          EXPECT_EQ(c.new_state.round_kind, rft::ROUND_KIND::LEADER);
          EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::ALL);
        }
      }

      WHEN("not a quorum") {
        self.votes_to_me.clear();
        auto c = rft::node_state_t::on_vote(self, s, self_addr, 2, from_s_addr, ae);
        THEN("wait") {
          EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
          EXPECT_EQ(c.new_state.round, self.round);
          EXPECT_EQ(c.new_state.round_kind, rft::ROUND_KIND::CANDIDATE);
          EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::NOBODY);
        }
      }
    }
  }
}

SCENARIO("node_state.on_append_entries") {
  rft::node_state_t self;
  rft::node_state_t from_s;

  rft::cluster_node self_addr;
  rft::cluster_node from_s_addr;

  from_s.round = 1;
  from_s_addr.set_name("from_s_addr");

  self.round = 1;
  self_addr.set_name("self_addr");
  self.leader = from_s_addr;

  rft::append_entries ae;
  ae.leader = from_s_addr;
  ae.round = from_s.round;

  rft::logdb::journal_ptr jrn{new rft::logdb::memory_journal()};

  WHEN("self == ELECTION") {
    self.round_kind = rft::ROUND_KIND::ELECTION;
    self.round = 0;
    WHEN("from==self.leader") {
      auto new_state =
          rft::node_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("follow to sender") {
        EXPECT_EQ(new_state.round_kind, rft::ROUND_KIND::FOLLOWER);
        EXPECT_EQ(new_state.leader.name(), from_s_addr.name());
        EXPECT_EQ(new_state.round, from_s.round);
      }
    }
    WHEN("from==self.leader") {
      self.leader.set_name("other name");
      auto new_state =
          rft::node_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("do nothing") {
        EXPECT_EQ(new_state.round_kind, self.round_kind);
        EXPECT_EQ(new_state.leader.name(), self.leader.name());
        EXPECT_EQ(new_state.round, self.round);
      }
    }
  }

  WHEN("self == FOLLOWER") {
    self.round_kind = rft::ROUND_KIND::FOLLOWER;
    self.leader.clear();
    self.round = 0;
    WHEN("round!=self.leader") {
      auto new_state =
          rft::node_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("follow to sender") {
        EXPECT_EQ(new_state.round_kind, rft::ROUND_KIND::FOLLOWER);
        EXPECT_EQ(new_state.leader.name(), from_s_addr.name());
        EXPECT_EQ(new_state.round, from_s.round);
      }
    }
    WHEN("from==self.leader") {
      self.leader.set_name("other name");
      auto new_state =
          rft::node_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("do nothing") {
        EXPECT_EQ(new_state.round_kind, self.round_kind);
        EXPECT_EQ(new_state.leader.name(), self.leader.name());
        EXPECT_EQ(new_state.round, self.round);
      }
    }
  }

  WHEN("self == LEADER") {
    self.round_kind = rft::ROUND_KIND::LEADER;
    WHEN("round>self.round") {
      self.round = 0;
      auto new_state =
          rft::node_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("follow to sender") {
        EXPECT_EQ(new_state.round_kind, rft::ROUND_KIND::FOLLOWER);
        EXPECT_EQ(new_state.leader.name(), from_s_addr.name());
        EXPECT_EQ(new_state.round, from_s.round);
      }
    }
    WHEN("sender.round>self.round") {
      ae.commited.round++;
      auto new_state =
          rft::node_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("follow to sender") {
        EXPECT_EQ(new_state.round_kind, rft::ROUND_KIND::FOLLOWER);
        EXPECT_EQ(new_state.leader.name(), from_s_addr.name());
        EXPECT_EQ(new_state.round, from_s.round);
      }
    }
  }

  WHEN("self == CANDIDATE") {
    self.round_kind = rft::ROUND_KIND::CANDIDATE;

    WHEN("round!=self.leader") {
      self.round = 0;
      auto new_state =
          rft::node_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("follow to sender") {
        EXPECT_EQ(new_state.round_kind, rft::ROUND_KIND::FOLLOWER);
        EXPECT_EQ(new_state.leader.name(), from_s_addr.name());
        EXPECT_EQ(new_state.round, from_s.round);
      }
    }
    WHEN("from==self.leader") {
      auto new_state =
          rft::node_state_t::on_append_entries(self, from_s_addr, jrn.get(), ae);
      THEN("do nothing") {
        EXPECT_EQ(new_state.round_kind, self.round_kind);
        EXPECT_EQ(new_state.leader.name(), self.leader.name());
        EXPECT_EQ(new_state.round, self.round);
      }
    }
  }
}

SCENARIO("node_state.on_heartbeat") {
  rft::node_state_t self;
  rft::node_state_t from_s;

  rft::cluster_node self_addr;
  rft::cluster_node from_s_addr;

  from_s.round = 1;
  from_s_addr.set_name("from_s_addr");

  self.round = 1;
  self_addr.set_name("self_addr");
  self.leader = from_s_addr;
  self.last_heartbeat_time = rft::clock_t::time_point();
  self.next_heartbeat_interval = std::chrono::milliseconds(0);
  rft::append_entries ae;
  ae.leader = from_s_addr;
  ae.round = from_s.round;

  WHEN("self == ELECTION") {
    self.round_kind = rft::ROUND_KIND::ELECTION;

    auto new_state = rft::node_state_t::on_heartbeat(self, self_addr, 2);
    THEN("be a follower") {
      EXPECT_EQ(new_state.round_kind, rft::ROUND_KIND::FOLLOWER);
      EXPECT_TRUE(new_state.leader.is_empty());
      EXPECT_EQ(new_state.round, self.round);
    }
  }

  WHEN("self == FOLLOWER") {
    self.round_kind = rft::ROUND_KIND::FOLLOWER;
    WHEN("alone in cluster") {
      auto new_state = rft::node_state_t::on_heartbeat(self, self_addr, 1);
      THEN("be a LEADER") {
        EXPECT_EQ(new_state.round_kind, rft::ROUND_KIND::LEADER);
        EXPECT_EQ(new_state.leader.name(), self_addr.name());
        EXPECT_EQ(new_state.round, self.round + 1);
      }
    }

    WHEN("not alone in cluster") {
      auto new_state = rft::node_state_t::on_heartbeat(self, self_addr, 2);
      THEN("be a LEADER") {
        EXPECT_EQ(new_state.round_kind, rft::ROUND_KIND::CANDIDATE);
        EXPECT_EQ(new_state.leader.name(), self_addr.name());
        EXPECT_EQ(new_state.election_round, 1);
        EXPECT_EQ(new_state.votes_to_me.size(), size_t(1));
        EXPECT_EQ(new_state.round, self.round + 1);
      }
    }
  }

  WHEN("self == CANDIDATE") {
    self.round_kind = rft::ROUND_KIND::CANDIDATE;
    WHEN("election_round>=5") {
      self.election_round = 5;
      auto new_state = rft::node_state_t::on_heartbeat(self, self_addr, 2);
      THEN("be a CANDIDATE") {
        EXPECT_EQ(new_state.round_kind, rft::ROUND_KIND::CANDIDATE);
        EXPECT_EQ(new_state.leader.name(), self_addr.name());
        EXPECT_EQ(new_state.round, self.round + 1);
        EXPECT_EQ(new_state.election_round, self.election_round);
      }
    }

    WHEN("not alone in cluster") {
      self.election_round = 4;
      auto new_state = rft::node_state_t::on_heartbeat(self, self_addr, 2);
      THEN("be a CANDIDATE") {
        EXPECT_EQ(new_state.round_kind, rft::ROUND_KIND::CANDIDATE);
        EXPECT_EQ(new_state.leader.name(), self_addr.name());
        EXPECT_EQ(new_state.round, self.round + 1);
        EXPECT_EQ(new_state.election_round, size_t(5));
      }
    }
  }
}
