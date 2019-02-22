#include "helpers.h"
#include <librft/consensus.h>
#include <catch.hpp>
#include <map>

struct mock_cluster : rft::abstract_cluster {
  void send_to(const rft::cluster_node &from, const rft::cluster_node &to,
               const rft::append_entries &m) override {
    auto it = _cluster.find(to);
    if (it != _cluster.end()) {
      _cluster[to]->recv(from, m);
    } else {
      throw std::logic_error("mock_cluster " + to.name());
    }
  }

  void send_all(const rft::cluster_node &from, const rft::append_entries &m) {
    for (auto &kv : _cluster) {
      if (kv.first != from) {
        kv.second->recv(from, m);
      }
    }
  }

  size_t size() override { return _cluster.size(); }

  std::map<rft::cluster_node, std::shared_ptr<rft::consensus>> _cluster;
};

TEST_CASE("consensus.election") {
  auto settings_0 = rft::node_settings().set_name("_0").set_election_timeout(
      std::chrono::milliseconds(500));

  /// SINGLE
  auto cluster = std::make_shared<mock_cluster>();
  auto c_0 = std::make_shared<rft::consensus>(settings_0, cluster,
                                              rft::logdb::memory_journal::make_new());
  cluster->_cluster[rft::cluster_node().set_name("_0")] = c_0;
  EXPECT_EQ(c_0->round(), rft::round_t(0));
  EXPECT_EQ(c_0->state(), rft::CONSENSUS_STATE::FOLLOWER);

  while (c_0->state() != rft::CONSENSUS_STATE::LEADER) {
    c_0->on_heartbeat();
    rft::utils::sleep_mls(100);
  }
  EXPECT_EQ(c_0->round(), rft::round_t(1));

  /// TWO NODES
  auto settings_1 = rft::node_settings().set_name("_1").set_election_timeout(
      std::chrono::milliseconds(500));
  auto c_1 = std::make_shared<rft::consensus>(settings_1, cluster,
                                              rft::logdb::memory_journal::make_new());
  cluster->_cluster[rft::cluster_node().set_name(settings_1.name())] = c_1;

  c_1->on_heartbeat();
  EXPECT_EQ(c_0->state(), rft::CONSENSUS_STATE::LEADER);
  EXPECT_EQ(c_1->state(), rft::CONSENSUS_STATE::FOLLOWER);
  EXPECT_EQ(c_0->round(), rft::round_t(1));
  EXPECT_EQ(c_1->round(), rft::round_t(1));
  EXPECT_EQ(c_1->get_leader(), c_0->get_leader());

  /// THREE NODES
  auto settings_2 = rft::node_settings().set_name("_2").set_election_timeout(
      std::chrono::milliseconds(500));
  auto c_2 = std::make_shared<rft::consensus>(settings_2, cluster,
                                              rft::logdb::memory_journal::make_new());
  cluster->_cluster[rft::cluster_node().set_name(settings_2.name())] = c_2;

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