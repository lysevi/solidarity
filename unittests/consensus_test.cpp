#include "helpers.h"
#include <librft/consensus.h>
#include <catch.hpp>
#include <condition_variable>
#include <map>
#include <shared_mutex>
#include <tuple>

class mock_cluster : public rft::abstract_cluster {
  struct message_t {
    rft::cluster_node from;
    rft::cluster_node to;
    rft::append_entries m;
  };

public:
  mock_cluster() {
    _worker_thread = std::thread([this]() { this->worker(); });
  }

  ~mock_cluster() {
    utils::logging::logger_info("~ mock_cluster ");
    while (_is_worker_active) {
      _stop_flag = true;
      _cond.notify_all();
    }
    _worker_thread.join();
  }

  void send_to(const rft::cluster_node &from, const rft::cluster_node &to,
               const rft::append_entries &m) override {
    std::unique_lock<std::mutex> ul(_tasks_locker);
    _tasks.push_back({from, to, m});
    _cond.notify_all();
  }

  void send_all(const rft::cluster_node &from, const rft::append_entries &m) {
    std::unique_lock<std::mutex> lg(_tasks_locker);
    for (auto &kv : _cluster) {
      if (kv.first != from) {
        _tasks.push_back({from, kv.first, m});
      }
    }
    _cond.notify_all();
  }

  void add_new(const rft::cluster_node &addr, const std::shared_ptr<rft::consensus> &c) {
    std::lock_guard<std::shared_mutex> lg(_cluster_locker);
    _cluster[addr] = c;
  }

  std::vector<std::shared_ptr<rft::consensus>>
  by_filter(std::function<bool(const std::shared_ptr<rft::consensus>)> pred) {
    std::shared_lock<std::shared_mutex> lg(_cluster_locker);
    std::vector<std::shared_ptr<rft::consensus>> result;
    result.reserve(_cluster.size());
    for (auto &kv : _cluster) {
      if (pred(kv.second)) {
        result.push_back(kv.second);
      }
    }
    return result;
  }

  void apply(std::function<void(const std::shared_ptr<rft::consensus>)> f) {
    std::shared_lock<std::shared_mutex> lg(_cluster_locker);
    for (auto &kv : _cluster) {
      f(kv.second);
    }
  }

  void on_heartbeat() {
    apply([](auto n) { return n->on_heartbeat(); });
  }

  void print_cluster() {
    utils::logging::logger_info("----------------------------");
    apply([](auto n) {
      utils::logging::logger_info("?: ", n->self_addr(), "{", n->state(), ":", n->round(),
                                  "}", " => ", n->get_leader());
    });
  }

  void erase_if(std::function<bool(const std::shared_ptr<rft::consensus>)> pred) {
    std::lock_guard<std::shared_mutex> lg(_cluster_locker);
    auto it = std::find_if(_cluster.begin(), _cluster.end(),
                           [pred](auto kv) { return pred(kv.second); });
    if (it != _cluster.end()) {
      _cluster.erase(it);
    } else {
      for (auto &v : _cluster) {
        if (pred(v.second)) {
          utils::logging::logger_info(1);
        }
      }
    }
  }

  size_t size() override {
    std::shared_lock<std::shared_mutex> lg(_cluster_locker);
    return _cluster.size();
  }

protected:
  void worker() {
    _is_worker_active = true;
    try {

      while (!_stop_flag) {
        std::vector<message_t> local_copy;
        {
          std::unique_lock<std::mutex> ul(_tasks_locker);
          _cond.wait(ul, [this] { return this->_stop_flag || !this->_tasks.empty(); });
          if (_stop_flag) {
            break;
          }
          if (_tasks.empty()) {
            continue;
          }
          local_copy.reserve(_tasks.size());
          std::copy(_tasks.begin(), _tasks.end(), std::back_inserter(local_copy));
          _tasks.clear();
        }
        for (auto &&v : local_copy) {
          std::shared_ptr<rft::consensus> target = nullptr;
          {
            std::shared_lock<std::shared_mutex> lg(_cluster_locker);
            auto it = _cluster.find(v.to);

            if (it == _cluster.cend()) {
              continue;
            } else {
              target = it->second;
            }
          }

          target->recv(v.from, v.m);
        }
        local_copy.clear();
      }
      _is_worker_active = false;
    } catch (std::exception &ex) {
      utils::logging::logger_fatal("mock_cluster: worker error:", ex.what());
    }
  }

private:
  std::thread _worker_thread;
  bool _stop_flag = false;
  bool _is_worker_active = false;
  std::mutex _tasks_locker;
  std::condition_variable _cond;
  /// from, to, message
  std::deque<message_t> _tasks;

  mutable std::shared_mutex _cluster_locker;
  std::map<rft::cluster_node, std::shared_ptr<rft::consensus>> _cluster;
};

bool is_leader_pred(const std::shared_ptr<rft::consensus> &v) {
  return v->state() == rft::ROUND_KIND::LEADER;
};

SCENARIO("raft.vote") {
  rft::node_state_t self;
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
        auto c = rft::node_state_t::on_vote(self, self_addr, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.leader.name(), from_s_addr.name());
          EXPECT_EQ(c.new_state.round, from_s.round);
          EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::SENDER);
        }
      }
    }
    WHEN("self == FOLLOWER") {
      self.round_kind = rft::ROUND_KIND::FOLLOWER;
      WHEN("leader.is_empty") {
        self.leader.clear();
        auto c = rft::node_state_t::on_vote(self, self_addr, 2, from_s_addr, ae);
        THEN("vote to sender") {
          EXPECT_EQ(c.new_state.leader.name(), from_s_addr.name());
          EXPECT_EQ(c.new_state.round, from_s.round);
          EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::SENDER);
        }
      }
      WHEN("!leader.is_empty") {
        self.leader.set_name("leader name");
        auto c = rft::node_state_t::on_vote(self, self_addr, 2, from_s_addr, ae);
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
        auto c = rft::node_state_t::on_vote(self, self_addr, 2, from_s_addr, ae);
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
        auto c = rft::node_state_t::on_vote(self, self_addr, 2, from_s_addr, ae);
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
      auto c = rft::node_state_t::on_vote(self, self_addr, 2, from_s_addr, ae);
      THEN("vote to self.leader") {
        EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
        EXPECT_EQ(c.new_state.round, ae.round);
        EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::SENDER);
      }
    }

    WHEN("self == FOLLOWER") {
      self.round_kind = rft::ROUND_KIND::FOLLOWER;
      self.round = from_s.round;

      auto c = rft::node_state_t::on_vote(self, self_addr, 2, from_s_addr, ae);
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
        self._election_to_me.insert(self_addr);
        auto c = rft::node_state_t::on_vote(self, self_addr, 2, from_s_addr, ae);
        THEN("make self a self.leader") {
          EXPECT_EQ(c.new_state.leader.name(), self.leader.name());
          EXPECT_EQ(c.new_state.round, self.round + 1);
          EXPECT_EQ(c.new_state.round_kind, rft::ROUND_KIND::LEADER);
          EXPECT_EQ(c.notify, rft::NOTIFY_TARGET::ALL);
        }
      }

      WHEN("not a quorum") {
        self._election_to_me.clear();
        auto c = rft::node_state_t::on_vote(self, self_addr, 2, from_s_addr, ae);
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

TEST_CASE("consensus.add_nodes") {
  auto cluster = std::make_shared<mock_cluster>();

  /// SINGLE
  auto settings_0 = rft::node_settings().set_name("_0").set_election_timeout(
      std::chrono::milliseconds(300));

  auto c_0 = std::make_shared<rft::consensus>(settings_0, cluster.get(),
                                              rft::logdb::memory_journal::make_new());
  cluster->add_new(rft::cluster_node().set_name("_0"), c_0);
  EXPECT_EQ(c_0->round(), rft::round_t(0));
  EXPECT_EQ(c_0->state(), rft::ROUND_KIND::FOLLOWER);

  while (c_0->state() != rft::ROUND_KIND::LEADER) {
    c_0->on_heartbeat();
    cluster->print_cluster();
  }
  EXPECT_EQ(c_0->round(), rft::round_t(1));

  /// TWO NODES
  auto settings_1 = rft::node_settings().set_name("_1").set_election_timeout(
      std::chrono::milliseconds(300));
  auto c_1 = std::make_shared<rft::consensus>(settings_1, cluster.get(),
                                              rft::logdb::memory_journal::make_new());
  cluster->add_new(rft::cluster_node().set_name(settings_1.name()), c_1);

  while (c_1->get_leader().name() != c_0->self_addr().name()) {
    cluster->on_heartbeat();
    cluster->print_cluster();
  }
  EXPECT_EQ(c_0->state(), rft::ROUND_KIND::LEADER);
  EXPECT_EQ(c_1->state(), rft::ROUND_KIND::FOLLOWER);
  EXPECT_EQ(c_0->round(), c_1->round());
  EXPECT_EQ(c_1->get_leader(), c_0->get_leader());

  /// THREE NODES
  auto settings_2 = rft::node_settings().set_name("_2").set_election_timeout(
      std::chrono::milliseconds(300));
  auto c_2 = std::make_shared<rft::consensus>(settings_2, cluster.get(),
                                              rft::logdb::memory_journal::make_new());
  cluster->add_new(rft::cluster_node().set_name(settings_2.name()), c_2);

  while (c_1->get_leader().name() != c_0->self_addr().name() ||
         c_2->get_leader().name() != c_0->self_addr().name()) {
    cluster->on_heartbeat();
    cluster->print_cluster();
  }

  EXPECT_EQ(c_0->state(), rft::ROUND_KIND::LEADER);
  EXPECT_EQ(c_1->state(), rft::ROUND_KIND::FOLLOWER);
  EXPECT_EQ(c_0->round(), c_1->round());
  EXPECT_EQ(c_2->round(), c_1->round());
  EXPECT_EQ(c_1->get_leader().name(), c_0->get_leader().name());
  cluster = nullptr;
}

TEST_CASE("consensus.election") {
  auto cluster = std::make_shared<mock_cluster>();

  size_t nodes_count = 4;
  SECTION("consensus.election.3") { nodes_count = 3; }
  SECTION("consensus.election.5") { nodes_count = 5; }
  SECTION("consensus.election.7") { nodes_count = 7; }
  SECTION("consensus.election.10") { nodes_count = 10; }
  SECTION("consensus.election.25") { nodes_count = 25; }

  for (size_t i = 0; i < nodes_count; ++i) {
    auto sett = rft::node_settings()
                    .set_name("_" + std::to_string(i))
                    .set_election_timeout(std::chrono::milliseconds(300));
    auto cons = std::make_shared<rft::consensus>(sett, cluster.get(),
                                                 rft::logdb::memory_journal::make_new());
    cluster->add_new(rft::cluster_node().set_name(sett.name()), cons);
  }
  rft::cluster_node last_leader;

  while (cluster->size() > 3) {

    while (true) {
      auto leaders = cluster->by_filter(is_leader_pred);
      if (leaders.size() > 1) {
        utils::logging::logger_fatal("consensus error!!!");
        cluster->print_cluster();
        EXPECT_FALSE(true);
        return;
      }
      if (leaders.size() == 1) {
        auto cur_leader = leaders.front()->self_addr();
        if (last_leader.is_empty()) {
          break;
        }
        if (cur_leader != last_leader) { // new leader election
          last_leader = cur_leader;
          break;
        }
      }
      cluster->on_heartbeat();
      cluster->print_cluster();
    }

    // kill the king...

    cluster->erase_if(is_leader_pred);
    utils::logging::logger_info("cluster size - ", cluster->size());
  }
}