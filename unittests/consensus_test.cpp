#include "helpers.h"
#include "mock_cluster.h"
#include <librft/consensus.h>
#include <libutils/logger.h>
#include <catch.hpp>

class mock_consumer : public rft::abstract_consensus_consumer {
public:
  void apply_cmd(const rft::command &cmd) override { last_cmd = cmd; }

  rft::command last_cmd;
};

bool is_leader_pred(const std::shared_ptr<rft::consensus> &v) {
  return v->state() == rft::ROUND_KIND::LEADER;
};

TEST_CASE("consensus.add_nodes") {
  auto cluster = std::make_shared<mock_cluster>();

  /// SINGLE
  auto settings_0 = rft::node_settings().set_name("_0").set_election_timeout(
      std::chrono::milliseconds(300));

  auto c_0_consumer = std::make_shared<mock_consumer>();
  auto c_0 = std::make_shared<rft::consensus>(settings_0, cluster.get(),
                                              rft::logdb::memory_journal::make_new(),
                                              c_0_consumer.get());

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
  auto c_1_consumer = std::make_shared<mock_consumer>();
  auto c_1 = std::make_shared<rft::consensus>(settings_1, cluster.get(),
                                              rft::logdb::memory_journal::make_new(),
                                              c_1_consumer.get());
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
  auto c_2_consumer = std::make_shared<mock_consumer>();
  auto c_2 = std::make_shared<rft::consensus>(settings_2, cluster.get(),
                                              rft::logdb::memory_journal::make_new(),
                                              c_2_consumer.get());

  cluster->add_new(rft::cluster_node().set_name(settings_2.name()), c_2);

  while (c_1->get_leader().name() != c_0->self_addr().name()
         || c_2->get_leader().name() != c_0->self_addr().name()) {
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

TEST_CASE("consensus") {
  auto cluster = std::make_shared<mock_cluster>();

  size_t nodes_count = 4;
  bool append_entries = false;
  SECTION("consensus.append") {
    append_entries = true;
    SECTION("consensus.append.3") { nodes_count = 3; }
    SECTION("consensus.append.5") { nodes_count = 5; }
    SECTION("consensus.append.7") { nodes_count = 7; }
    SECTION("consensus.append.10") { nodes_count = 10; }
    SECTION("consensus.append.15") { nodes_count = 15; }
#if !defined(DEBUG)
    SECTION("consensus.election.25") { nodes_count = 25; }
#endif
  }

  SECTION("consensus.election.election") {
    append_entries = false;
    SECTION("consensus.election.3") { nodes_count = 3; }
    SECTION("consensus.election.5") { nodes_count = 5; }
    SECTION("consensus.election.7") { nodes_count = 7; }
    SECTION("consensus.election.10") { nodes_count = 10; }
    SECTION("consensus.election.15") { nodes_count = 15; }
#if !defined(DEBUG)
    SECTION("consensus.election.25") { nodes_count = 25; }
#endif
  }

  std::vector<std::shared_ptr<mock_consumer>> consumers;
  consumers.reserve(nodes_count);

  auto et = std::chrono::milliseconds(300);
  /*if (append_entries) {
    et = std::chrono::milliseconds(1500);
  }*/
  for (size_t i = 0; i < nodes_count; ++i) {
    auto sett
        = rft::node_settings().set_name("_" + std::to_string(i)).set_election_timeout(et);
    auto c = std::make_shared<mock_consumer>();
    consumers.push_back(c);
    auto cons = std::make_shared<rft::consensus>(
        sett, cluster.get(), rft::logdb::memory_journal::make_new(), c.get());
    cluster->add_new(rft::cluster_node().set_name(sett.name()), cons);
  }
  rft::cluster_node last_leader;
  rft::command cmd;
  cmd.data.resize(1);
  cmd.data[0] = 0;
  while (cluster->size() > 2) {
    std::vector<std::shared_ptr<rft::consensus>> leaders;
    while (true) {
      leaders = cluster->by_filter(is_leader_pred);
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
    if (!append_entries) {
      cluster->erase_if(is_leader_pred);
      utils::logging::logger_info("cluster size - ", cluster->size());
    } else {
      auto data_eq = [&cmd](const std::shared_ptr<mock_consumer> &c) -> bool {
        return c->last_cmd.data == cmd.data;
      };
      for (int i = 0; i < 10; ++i) {
        cmd.data[0]++;
        leaders[0]->add_command(cmd);
        while (true) {
          cluster->on_heartbeat();
          bool all_of = std::all_of(consumers.cbegin(), consumers.cend(), data_eq);
          if (all_of) {
            break;
          }
        }
      }
      break;
    }
  }
}