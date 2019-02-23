#include "helpers.h"
#include <librft/consensus.h>
#include <catch.hpp>
#include <condition_variable>
#include <map>
#include <mutex>
#include <tuple>

struct mock_cluster : rft::abstract_cluster {
public:
  mock_cluster() {
    _worker_thread = std::thread([this]() { this->worker(); });
  }

  ~mock_cluster() {
    while (_is_worker_active) {
      _stop_flag = true;
      _cond.notify_all();
    }
    _worker_thread.join();
  }

  void send_to(const rft::cluster_node &from, const rft::cluster_node &to,
               const rft::append_entries &m) override {
    std::unique_lock<std::mutex> ul(_tasks_locker);
    _tasks.push_back(std::tie(from, to, m));
    _cond.notify_all();
  }

  void send_all(const rft::cluster_node &from, const rft::append_entries &m) {
    std::unique_lock<std::mutex> lg(_tasks_locker);
    for (auto &kv : _cluster) {
      _tasks.push_back(std::tie(from, kv.first, m));
    }
    _cond.notify_all();
  }

  void add_new(const rft::cluster_node &addr, const std::shared_ptr<rft::consensus> &c) {
    std::lock_guard<std::mutex> lg(_cluster_locker);
    _cluster[addr] = c;
  }

  void erase_if(std::function<bool(const std::shared_ptr<rft::consensus>)> pred) {
    std::lock_guard<std::mutex> lg(_cluster_locker);
    auto it = std::find_if(_cluster.begin(), _cluster.end(),
                           [pred](auto kv) { return pred(kv.second); });
    if (it != _cluster.end()) {
      _cluster.erase(it);
    }
  }

  size_t size() override {
    std::lock_guard<std::mutex> lg(_cluster_locker);
    return _cluster.size();
  }

protected:
  void worker() {
    _is_worker_active = true;
    try {

      while (!_stop_flag) {
        std::vector<std::tuple<rft::cluster_node, rft::cluster_node, rft::append_entries>>
            local_copy;
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
        for (auto v : local_copy) {
          auto it = _cluster.find(std::get<1>(v));
          if (it != _cluster.end()) {
            it->second->recv(std::get<0>(v), std::get<2>(v));
          } /* else {
             throw std::logic_error("unknow sender");
           }*/
        }
        local_copy.clear();
      }
      _is_worker_active = false;
    } catch (std::exception &ex) {
      rft::utils::logging::logger_fatal("mock_cluster: worker error:", ex.what());
    }
  }

private:
  std::thread _worker_thread;
  bool _stop_flag = false;
  bool _is_worker_active = false;
  std::mutex _tasks_locker;
  std::condition_variable _cond;
  /// from, to, message
  std::deque<std::tuple<rft::cluster_node, rft::cluster_node, rft::append_entries>>
      _tasks;

  mutable std::mutex _cluster_locker;
  std::map<rft::cluster_node, std::shared_ptr<rft::consensus>> _cluster;
};

TEST_CASE("consensus.add_nodes") {
  std::vector<std::shared_ptr<rft::consensus>> all_nodes;
  auto cluster = std::make_shared<mock_cluster>();

  /// SINGLE
  auto settings_0 = rft::node_settings().set_name("_0").set_election_timeout(
      std::chrono::milliseconds(3000));

  auto c_0 = std::make_shared<rft::consensus>(settings_0, cluster,
                                              rft::logdb::memory_journal::make_new());
  all_nodes.push_back(c_0);
  cluster->add_new(rft::cluster_node().set_name("_0"), c_0);
  EXPECT_EQ(c_0->round(), rft::round_t(0));
  EXPECT_EQ(c_0->state(), rft::CONSENSUS_STATE::FOLLOWER);

  while (c_0->state() != rft::CONSENSUS_STATE::LEADER) {
    c_0->on_heartbeat();
    rft::utils::sleep_mls(100);
  }
  EXPECT_EQ(c_0->round(), rft::round_t(1));

  /// TWO NODES
  auto settings_1 = rft::node_settings().set_name("_1").set_election_timeout(
      std::chrono::milliseconds(3000));
  auto c_1 = std::make_shared<rft::consensus>(settings_1, cluster,
                                              rft::logdb::memory_journal::make_new());
  all_nodes.push_back(c_1);
  cluster->add_new(rft::cluster_node().set_name(settings_1.name()), c_1);

  while (c_1->get_leader() != c_0->get_leader()) {
    c_1->on_heartbeat();
    c_0->on_heartbeat();
  }
  EXPECT_EQ(c_0->state(), rft::CONSENSUS_STATE::LEADER);
  EXPECT_EQ(c_1->state(), rft::CONSENSUS_STATE::FOLLOWER);
  EXPECT_EQ(c_0->round(), rft::round_t(1));
  EXPECT_EQ(c_1->round(), rft::round_t(1));
  EXPECT_EQ(c_1->get_leader(), c_0->get_leader());

  /// THREE NODES
  auto settings_2 = rft::node_settings().set_name("_2").set_election_timeout(
      std::chrono::milliseconds(3000));
  auto c_2 = std::make_shared<rft::consensus>(settings_2, cluster,
                                              rft::logdb::memory_journal::make_new());
  all_nodes.push_back(c_2);
  cluster->add_new(rft::cluster_node().set_name(settings_2.name()), c_2);

  while (c_2->get_leader().is_empty() || c_1->state() != rft::CONSENSUS_STATE::FOLLOWER) {
    c_0->on_heartbeat();
    c_1->on_heartbeat();
    c_2->on_heartbeat();
  }

  EXPECT_EQ(c_0->state(), rft::CONSENSUS_STATE::LEADER);
  EXPECT_EQ(c_1->state(), rft::CONSENSUS_STATE::FOLLOWER);
  EXPECT_EQ(c_0->round(), rft::round_t(1));
  EXPECT_EQ(c_1->round(), rft::round_t(1));
  EXPECT_EQ(c_1->get_leader(), c_0->get_leader());
}

TEST_CASE("consensus.election") {
  std::vector<std::shared_ptr<rft::consensus>> all_nodes;
  auto cluster = std::make_shared<mock_cluster>();

  size_t nodes_count = 4;
  SECTION("consensus.election.3") { nodes_count = 3; }
  SECTION("consensus.election.4") { nodes_count = 4; }
  SECTION("consensus.election.5") { nodes_count = 5; }

  for (size_t i = 0; i < nodes_count; ++i) {
    auto sett = rft::node_settings()
                    .set_name("_" + std::to_string(i))
                    .set_election_timeout(std::chrono::milliseconds(500));
    auto cons = std::make_shared<rft::consensus>(sett, cluster,
                                                 rft::logdb::memory_journal::make_new());
    cluster->add_new(rft::cluster_node().set_name(sett.name()), cons);
    all_nodes.push_back(cons);
  }
  rft::cluster_node last_leader;
  while (cluster->size() > 3) {

    while (true) {
      std::map<rft::cluster_node, size_t> leaders;
      for (auto v : all_nodes) {
        if (v->get_leader().is_empty()) {
          continue;
        }
        if (leaders.find(v->get_leader()) == leaders.end()) {
          leaders[v->get_leader()] = 0;
        }
        leaders[v->get_leader()]++;
      }
      if (leaders.size() == 1) {
        auto cur_leader = leaders.begin()->first;
        if (last_leader.is_empty() || cur_leader != last_leader) { // new leader election
          last_leader = cur_leader;
          break;
        }
      }
      std::for_each(all_nodes.begin(), all_nodes.end(),
                    [](auto n) { return n->on_heartbeat(); });
      std::for_each(all_nodes.begin(), all_nodes.end(), [](auto n) {
        rft::utils::logging::logger_info("?: ", n->self_addr(), ": -> ", n->get_leader());
      });
    }

    { // kill the king...
      auto pred = [](auto v) { return v->state() == rft::CONSENSUS_STATE::LEADER; };

      cluster->erase_if(pred);
      {
        auto it = std::find_if(all_nodes.begin(), all_nodes.end(), [](auto v) {
          return v->state() == rft::CONSENSUS_STATE::LEADER;
        });
        all_nodes.erase(it);
      }
      rft::utils::logging::logger_info("cluster size - ", cluster->size());
    }
  }
}