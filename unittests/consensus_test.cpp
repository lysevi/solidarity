#include "helpers.h"
#include <librft/consensus.h>
#include <catch.hpp>
#include <condition_variable>
#include <map>
#include <shared_mutex>
#include <tuple>

class mock_cluster : public rft::abstract_cluster {
public:
  mock_cluster() {
    _worker_thread = std::thread([this]() { this->worker(); });
  }

  ~mock_cluster() {
    rft::utils::logging::logger_info("~ mock_cluster ");
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
      if (kv.first != from) {
        _tasks.push_back(std::tie(from, kv.first, m));
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

  void erase_if(std::function<bool(const std::shared_ptr<rft::consensus>)> pred) {
    std::lock_guard<std::shared_mutex> lg(_cluster_locker);
    auto it = std::find_if(_cluster.begin(), _cluster.end(),
                           [pred](auto kv) { return pred(kv.second); });
    if (it != _cluster.end()) {
      _cluster.erase(it);
    } else {
      for (auto &v : _cluster) {
        if (pred(v.second)) {
          rft::utils::logging::logger_info(1);
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

  mutable std::shared_mutex _cluster_locker;
  std::map<rft::cluster_node, std::shared_ptr<rft::consensus>> _cluster;
};

bool is_leader_pred(const std::shared_ptr<rft::consensus> &v) {
  return v->state() == rft::CONSENSUS_STATE::LEADER;
};

TEST_CASE("consensus.add_nodes") {
  auto cluster = std::make_shared<mock_cluster>();

  /// SINGLE
  auto settings_0 = rft::node_settings().set_name("_0").set_election_timeout(
      std::chrono::milliseconds(3000));

  auto c_0 = std::make_shared<rft::consensus>(settings_0, cluster,
                                              rft::logdb::memory_journal::make_new());
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
  cluster->add_new(rft::cluster_node().set_name(settings_1.name()), c_1);

  while (c_1->get_leader().name() != c_0->self_addr().name()) {
    c_1->on_heartbeat();
    c_0->on_heartbeat();
  }
  EXPECT_EQ(c_0->state(), rft::CONSENSUS_STATE::LEADER);
  EXPECT_EQ(c_1->state(), rft::CONSENSUS_STATE::FOLLOWER);
  EXPECT_EQ(c_0->round(), c_1->round());
  EXPECT_EQ(c_1->get_leader(), c_0->get_leader());

  /// THREE NODES
  auto settings_2 = rft::node_settings().set_name("_2").set_election_timeout(
      std::chrono::milliseconds(3000));
  auto c_2 = std::make_shared<rft::consensus>(settings_2, cluster,
                                              rft::logdb::memory_journal::make_new());
  cluster->add_new(rft::cluster_node().set_name(settings_2.name()), c_2);

  while (c_1->get_leader().name() != c_0->self_addr().name() ||
         c_2->get_leader().name() != c_0->self_addr().name()) {
    c_0->on_heartbeat();
    c_1->on_heartbeat();
    c_2->on_heartbeat();
  }

  EXPECT_EQ(c_0->state(), rft::CONSENSUS_STATE::LEADER);
  EXPECT_EQ(c_1->state(), rft::CONSENSUS_STATE::FOLLOWER);
  EXPECT_EQ(c_0->round(), c_1->round());
  EXPECT_EQ(c_2->round(), c_1->round());
  EXPECT_EQ(c_1->get_leader(), c_0->get_leader());
  cluster = nullptr;
}

TEST_CASE("consensus.election") {
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
  }
  rft::cluster_node last_leader;

  while (cluster->size() > 3) {

    while (true) {
      auto leaders = cluster->by_filter(is_leader_pred);

      if (leaders.size() == 1) {
        auto cur_leader = leaders.at(0)->self_addr();
        if (last_leader.is_empty() || cur_leader != last_leader) { // new leader election
          last_leader = cur_leader;
          break;
        }
      }
      cluster->apply([](auto n) { return n->on_heartbeat(); });
      cluster->apply([](auto n) {
        rft::utils::logging::logger_info("?: ", n->self_addr(), ": -> ", n->get_leader());
      });
    }

    // kill the king...

    cluster->erase_if(is_leader_pred);
    rft::utils::logging::logger_info("cluster size - ", cluster->size());
  }
}