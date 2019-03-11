#include "helpers.h"
#include "mock_cluster.h"
#include <librft/consensus.h>
#include <libutils/logger.h>
#include <catch.hpp>

class mock_consumer final : public rft::abstract_consensus_consumer {
public:
  void apply_cmd(const rft::command &cmd) override { last_cmd = cmd; }
  void reset() override { last_cmd.data.clear(); }
  rft::command last_cmd;
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
  EXPECT_EQ(c_0->term(), rft::term_t(0));
  EXPECT_EQ(c_0->state(), rft::NODE_KIND::FOLLOWER);

  while (c_0->state() != rft::NODE_KIND::LEADER) {
    c_0->on_heartbeat();
    cluster->print_cluster();
  }
  EXPECT_EQ(c_0->term(), rft::term_t(1));

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
  EXPECT_EQ(c_0->state(), rft::NODE_KIND::LEADER);
  EXPECT_EQ(c_1->state(), rft::NODE_KIND::FOLLOWER);
  EXPECT_EQ(c_0->term(), c_1->term());
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

  EXPECT_EQ(c_0->state(), rft::NODE_KIND::LEADER);
  EXPECT_EQ(c_1->state(), rft::NODE_KIND::FOLLOWER);
  EXPECT_EQ(c_0->term(), c_1->term());
  EXPECT_EQ(c_2->term(), c_1->term());
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
#if !defined(DEBUG)
    SECTION("consensus.append.15") { nodes_count = 15; }
#endif
  }

  SECTION("consensus.election.election") {
    append_entries = false;
    SECTION("consensus.election.3") { nodes_count = 3; }
    SECTION("consensus.election.5") { nodes_count = 5; }
    SECTION("consensus.election.7") { nodes_count = 7; }
    SECTION("consensus.election.10") { nodes_count = 10; }
#if !defined(DEBUG)
    SECTION("consensus.election.15") { nodes_count = 15; }
#endif
  }

  std::vector<std::shared_ptr<mock_consumer>> consumers;
  consumers.reserve(nodes_count);

  auto et = std::chrono::milliseconds(300);
  for (size_t i = 0; i < nodes_count; ++i) {
    auto nname = "_" + std::to_string(i);
    auto sett = rft::node_settings().set_name(nname).set_election_timeout(et);
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

  auto data_eq = [&cmd](const std::shared_ptr<mock_consumer> &c) -> bool {
    return c->last_cmd.data == cmd.data;
  };

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
        auto followers
            = cluster->by_filter([cur_leader](const std::shared_ptr<rft::consensus> &v) {
                return v->get_leader() == cur_leader;
              });
        if (last_leader.is_empty() && followers.size() == cluster->size()) {
          last_leader = cur_leader;
          break;
        }
        if (cur_leader != last_leader
            && followers.size() == cluster->size()) { // new leader election
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
  cluster = nullptr;
  consumers.clear();
}

TEST_CASE("consensus.replication") {
  using rft::cluster_node;
  using rft::consensus;
  using rft::logdb::memory_journal;

  auto cluster = std::make_shared<mock_cluster>();

  size_t exists_nodes_count = 1;
  size_t new_nodes_count = 1;

  SECTION("consensus.replication.1x1") {
    exists_nodes_count = 1;
    new_nodes_count = 1;
  }
  SECTION("consensus.replication.1x2") {
    exists_nodes_count = 1;
    new_nodes_count = 2;
  }
  SECTION("consensus.replication.1x3") {
    exists_nodes_count = 1;
    new_nodes_count = 3;
  }

  SECTION("consensus.replication.2x1") {
    exists_nodes_count = 2;
    new_nodes_count = 1;
  }
  SECTION("consensus.replication.2x2") {
    exists_nodes_count = 2;
    new_nodes_count = 2;
  }
  SECTION("consensus.replication.3x3") {
    exists_nodes_count = 3;
    new_nodes_count = 3;
  }

  std::vector<std::shared_ptr<mock_consumer>> consumers;
  consumers.reserve(exists_nodes_count);

  auto et = std::chrono::milliseconds(300);

  for (size_t i = 0; i < exists_nodes_count; ++i) {
    auto nname = "_" + std::to_string(i);
    auto sett = rft::node_settings().set_name(nname).set_election_timeout(et);
    auto consumer = std::make_shared<mock_consumer>();
    consumers.push_back(consumer);
    auto cons = std::make_shared<consensus>(sett, cluster.get(),
                                            memory_journal::make_new(), consumer.get());
    cluster->add_new(cluster_node().set_name(sett.name()), cons);
  }
  rft::command cmd;
  cmd.data.resize(1);
  cmd.data[0] = 0;

  auto data_eq = [&cmd](const std::shared_ptr<mock_consumer> &c) -> bool {
    return c->last_cmd.data == cmd.data;
  };

  cluster->wait_leader_eletion();

  std::vector<std::shared_ptr<rft::consensus>> leaders
      = cluster->by_filter(is_leader_pred);
  EXPECT_EQ(leaders.size(), size_t(1));

  for (int i = 0; i < 10; ++i) {
    cmd.data[0]++;
    leaders[0]->add_command(cmd);
    while (true) {
      cluster->on_heartbeat();
      auto replicated_on = std::count_if(consumers.cbegin(), consumers.cend(), data_eq);
      if (size_t(replicated_on) == consumers.size()) {
        break;
      }
    }
  }

  for (int i = 0; i < new_nodes_count; ++i) {
    auto nname = "_" + std::to_string(i + 1 + exists_nodes_count);
    auto sett = rft::node_settings().set_name(nname).set_election_timeout(et);
    auto consumer = std::make_shared<mock_consumer>();
    consumers.push_back(consumer);
    auto cons = std::make_shared<consensus>(sett, cluster.get(),
                                            memory_journal::make_new(), consumer.get());
    cluster->add_new(cluster_node().set_name(sett.name()), cons);
    cluster->wait_leader_eletion();
  }

  while (true) {
    auto replicated_on = std::count_if(consumers.cbegin(), consumers.cend(), data_eq);
    if (size_t(replicated_on) == consumers.size()) {
      break;
    }
    utils::logging::logger_info("[test] replicated_on: ", replicated_on);
    cluster->on_heartbeat();
  }

  cluster = nullptr;
  consumers.clear();
}

TEST_CASE("consensus.rollback") {
  using rft::cluster_node;
  using rft::consensus;
  using rft::logdb::memory_journal;

  auto cluster = std::make_shared<mock_cluster>();

  size_t exists_nodes_count = 2;

  std::vector<std::shared_ptr<mock_consumer>> consumers;
  consumers.reserve(exists_nodes_count);

  auto et = std::chrono::milliseconds(300);

  for (size_t i = 0; i < exists_nodes_count; ++i) {
    auto nname = "_" + std::to_string(i);
    auto sett = rft::node_settings().set_name(nname).set_election_timeout(et);
    auto consumer = std::make_shared<mock_consumer>();
    consumers.push_back(consumer);
    auto cons = std::make_shared<consensus>(sett, cluster.get(),
                                            memory_journal::make_new(), consumer.get());
    cluster->add_new(cluster_node().set_name(sett.name()), cons);
  }
  rft::command cmd;
  cmd.data.resize(1);
  cmd.data[0] = 0;

  auto data_eq = [&cmd](const std::shared_ptr<mock_consumer> &c) -> bool {
    return c->last_cmd.data == cmd.data;
  };

  cluster->wait_leader_eletion();
  {
    std::vector<std::shared_ptr<rft::consensus>> leaders
        = cluster->by_filter(is_leader_pred);
    EXPECT_EQ(leaders.size(), size_t(1));

    for (int i = 0; i < 10; ++i) {
      cmd.data[0]++;
      leaders[0]->add_command(cmd);
      while (true) {
        cluster->on_heartbeat();
        auto replicated_on = std::count_if(consumers.cbegin(), consumers.cend(), data_eq);
        if (size_t(replicated_on) == consumers.size()) {
          break;
        }
      }
    }
  }

  auto cluster2 = cluster->split(1);
  while (!cluster->is_leader_eletion_complete()
         && !cluster->is_leader_eletion_complete()) {
    cluster->on_heartbeat();
    cluster2->on_heartbeat();

    utils::logging::logger_info("[test] cluster 1:");
    cluster->print_cluster();

    utils::logging::logger_info("[test] cluster 2:");
    cluster2->print_cluster();
  }

  rft::command cmd2;
  cmd2.data.resize(1);
  cmd2.data[0] = 0;

  std::vector<std::shared_ptr<rft::consensus>> leaders1
      = cluster->by_filter(is_leader_pred);
  EXPECT_EQ(leaders1.size(), size_t(1));

  std::vector<std::shared_ptr<rft::consensus>> leaders2
      = cluster2->by_filter(is_leader_pred);
  EXPECT_EQ(leaders2.size(), size_t(1));

  for (int i = 0; i < 10; ++i) {
    cmd.data[0]++;
    leaders1[0]->add_command(cmd);
    cmd.data[0]++;
    leaders1[0]->add_command(cmd);
    cmd2.data[0] += 5;
    leaders2[0]->add_command(cmd2);
  }

  EXPECT_FALSE(consumers.front()->last_cmd.data == consumers.back()->last_cmd.data);

  cluster->union_with(cluster2);
  cluster->wait_leader_eletion(2);

  while (true) {
    cluster->on_heartbeat();
    if (consumers.front()->last_cmd.data == consumers.back()->last_cmd.data) {
      break;
    }
  }

  cluster = nullptr;
  cluster2 = nullptr;
  consumers.clear();
}