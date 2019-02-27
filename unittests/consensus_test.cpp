#include "helpers.h"
#include "mock_cluster.h"
#include <librft/consensus.h>
#include <catch.hpp>

bool is_leader_pred(const std::shared_ptr<rft::consensus> &v) {
  return v->state() == rft::ROUND_KIND::LEADER;
};

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
#if  !defined(DEBUG)
  SECTION("consensus.election.25") { nodes_count = 25; }
#endif
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