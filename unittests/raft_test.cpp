#include "helpers.h"
#include "mock_cluster.h"
#include "mock_state_machine.h"
#include <catch.hpp>
#include <solidarity/raft.h>
#include <solidarity/utils/logger.h>

TEST_CASE("raft.quorum calculation", "[raft]") {
  EXPECT_EQ(solidarity::quorum_for_cluster(3, 0.5), 2);
  EXPECT_EQ(solidarity::quorum_for_cluster(4, 0.5), 3);
  EXPECT_EQ(solidarity::quorum_for_cluster(4, 1.0), 4);
  EXPECT_EQ(solidarity::quorum_for_cluster(2, 0.5), 2);
  EXPECT_EQ(solidarity::quorum_for_cluster(5, 0.5), 3);
}

TEST_CASE("raft.add_nodes", "[raft]") {
  auto cluster = std::make_shared<mock_cluster>();

  /// SINGLE
  auto settings_0 = solidarity::raft_settings_t().set_name("_0").set_election_timeout(
      std::chrono::milliseconds(300));

  auto c_0_consumer = std::make_shared<mock_state_machine>();
  auto c_0
      = std::make_shared<solidarity::raft>(settings_0,
                                           cluster.get(),
                                           solidarity::logdb::memory_journal::make_new(),
                                           c_0_consumer.get());

  cluster->add_new("_0", c_0);
  EXPECT_EQ(c_0->term(), solidarity::UNDEFINED_TERM);
  EXPECT_EQ(c_0->kind(), solidarity::NODE_KIND::FOLLOWER);

  while (c_0->kind() != solidarity::NODE_KIND::LEADER) {
    c_0->heartbeat();
    cluster->print_cluster();
  }
  EXPECT_EQ(c_0->term(), solidarity::term_t(0));

  /// TWO NODES
  auto settings_1 = solidarity::raft_settings_t().set_name("_1").set_election_timeout(
      std::chrono::milliseconds(300));
  auto c_1_consumer = std::make_shared<mock_state_machine>();
  auto c_1
      = std::make_shared<solidarity::raft>(settings_1,
                                           cluster.get(),
                                           solidarity::logdb::memory_journal::make_new(),
                                           c_1_consumer.get());
  cluster->add_new(settings_1.name(), c_1);

  while (c_1->get_leader() != c_0->self_addr()) {
    cluster->heartbeat();
    cluster->print_cluster();
  }
  EXPECT_EQ(c_0->kind(), solidarity::NODE_KIND::LEADER);
  EXPECT_EQ(c_1->kind(), solidarity::NODE_KIND::FOLLOWER);
  EXPECT_EQ(c_0->term(), c_1->term());
  EXPECT_EQ(c_1->get_leader(), c_0->get_leader());

  /// THREE NODES
  auto settings_2 = solidarity::raft_settings_t().set_name("_2").set_election_timeout(
      std::chrono::milliseconds(300));
  auto c_2_consumer = std::make_shared<mock_state_machine>();
  auto c_2
      = std::make_shared<solidarity::raft>(settings_2,
                                           cluster.get(),
                                           solidarity::logdb::memory_journal::make_new(),
                                           c_2_consumer.get());

  cluster->add_new(settings_2.name(), c_2);

  while (c_1->get_leader() != c_0->self_addr() || c_2->get_leader() != c_0->self_addr()) {
    cluster->heartbeat();
    cluster->print_cluster();
  }

  EXPECT_EQ(c_0->kind(), solidarity::NODE_KIND::LEADER);
  EXPECT_EQ(c_1->kind(), solidarity::NODE_KIND::FOLLOWER);
  EXPECT_EQ(c_0->term(), c_1->term());
  EXPECT_EQ(c_2->term(), c_1->term());
  EXPECT_EQ(c_1->get_leader(), c_0->get_leader());
  cluster = nullptr;
}

TEST_CASE("raft", "[raft]") {
  auto cluster = std::make_shared<mock_cluster>();

  size_t nodes_count = 4;
  bool append_entries = false;
  SECTION("raft.append") {
    append_entries = true;
    SECTION("raft.append.3") { nodes_count = 3; }
    SECTION("raft.append.5") { nodes_count = 5; }
    SECTION("raft.append.7") { nodes_count = 7; }
    SECTION("raft.append.10") { nodes_count = 10; }
#if !defined(DEBUG)
    SECTION("raft.append.15") { nodes_count = 15; }
#endif
  }

  SECTION("raft.election.election") {
    append_entries = false;
    SECTION("raft.election.3") { nodes_count = 3; }
    SECTION("raft.election.5") { nodes_count = 5; }
    SECTION("raft.election.7") { nodes_count = 7; }
    SECTION("raft.election.10") { nodes_count = 10; }
#if !defined(DEBUG)
    SECTION("raft.election.15") { nodes_count = 15; }
#endif
  }

  std::vector<std::shared_ptr<mock_state_machine>> consumers;
  consumers.reserve(nodes_count);

  auto et = std::chrono::milliseconds(300);
  for (size_t i = 0; i < nodes_count; ++i) {
    auto nname = "_" + std::to_string(i);
    auto sett = solidarity::raft_settings_t().set_name(nname).set_election_timeout(et);
    auto c = std::make_shared<mock_state_machine>();
    consumers.push_back(c);
    auto cons = std::make_shared<solidarity::raft>(
        sett, cluster.get(), solidarity::logdb::memory_journal::make_new(), c.get());
    cluster->add_new(sett.name(), cons);
  }
  solidarity::node_name last_leader;
  solidarity::command_t cmd(1);
  cmd.data[0] = 0;

  auto data_eq = [&cmd](const std::shared_ptr<mock_state_machine> &c) -> bool {
    return c->get_last_cmd().data == cmd.data;
  };

  while (cluster->size() > 2) {
    std::vector<std::shared_ptr<solidarity::raft>> leaders;
    while (true) {
      leaders = cluster->by_filter(is_leader_pred);
      if (leaders.size() > 1) {
        std::unordered_set<solidarity::term_t> terms;
        for (auto &c : leaders) {
          terms.insert(c->state().term);
        }
        if (terms.size() == 1) {
          solidarity::utils::logging::logger_fatal("raft error!!!");
          cluster->print_cluster();
          EXPECT_FALSE(true);
          return;
        }
      }
      if (leaders.size() == 1) {
        auto cur_leader = leaders.front()->self_addr();
        auto followers = cluster->by_filter(
            [cur_leader](const std::shared_ptr<solidarity::raft> &v) {
              return v->get_leader() == cur_leader;
            });
        if (last_leader.empty() && followers.size() == cluster->size()) {
          last_leader = cur_leader;
          break;
        }
        if (cur_leader != last_leader
            && followers.size() == cluster->size()) { // new leader election
          last_leader = cur_leader;
          break;
        }
      }
      cluster->heartbeat();
      cluster->print_cluster();
    }

    // kill the king...
    if (!append_entries) {
      cluster->erase_if(is_leader_pred);
      solidarity::utils::logging::logger_info("cluster size - ", cluster->size());
    } else {
      const size_t attempts_to_add = 500;
      // TODO implement this method add_command in mock_cluster and use it in all testes
      for (int i = 0; i < 10; ++i) {
        bool cur_cmd_is_replicated = false;
        while (!cur_cmd_is_replicated) {
          leaders = cluster->by_filter(is_leader_pred);
          if (leaders.size() != size_t(1)) {
            cluster->wait_leader_eletion();
            leaders = cluster->by_filter(is_leader_pred);
          }
          cmd.data[0]++;
          auto st = leaders[0]->add_command(cmd);
          EXPECT_EQ(st, solidarity::ERROR_CODE::OK);
          for (size_t j = 0; j < attempts_to_add; ++j) {
            cluster->print_cluster();
            cluster->heartbeat();
            bool all_of = std::all_of(consumers.cbegin(), consumers.cend(), data_eq);
            if (all_of) {
              cur_cmd_is_replicated = true;
              break;
            }
          }
          EXPECT_TRUE(cur_cmd_is_replicated);
        }
      }
      break;
    }
  }
  cluster = nullptr;
  consumers.clear();
}

TEST_CASE("raft.replication", "[raft]") {
  using solidarity::node_name;
  using solidarity::raft;
  using solidarity::logdb::memory_journal;

  auto cluster = std::make_shared<mock_cluster>();

  size_t exists_nodes_count = 1;
  size_t new_nodes_count = 1;

  SECTION("raft.replication.2x1") {
    exists_nodes_count = 2;
    new_nodes_count = 1;
  }
  SECTION("raft.replication.2x2") {
    exists_nodes_count = 2;
    new_nodes_count = 2;
  }
  SECTION("raft.replication.3x3") {
    exists_nodes_count = 3;
    new_nodes_count = 3;
  }

  std::vector<std::shared_ptr<mock_state_machine>> consumers;
  consumers.reserve(exists_nodes_count);

  auto et = std::chrono::milliseconds(400);

  for (size_t i = 0; i < exists_nodes_count; ++i) {
    auto nname = "_" + std::to_string(i);
    auto sett = solidarity::raft_settings_t().set_name(nname).set_election_timeout(et);
    auto state_machine = std::make_shared<mock_state_machine>();
    consumers.push_back(state_machine);
    auto cons = std::make_shared<raft>(
        sett, cluster.get(), memory_journal::make_new(), state_machine.get());
    cluster->add_new(sett.name(), cons);
  }
  solidarity::command_t cmd(1);
  cmd.data[0] = 0;

  auto data_eq = [&cmd](const std::shared_ptr<mock_state_machine> &c) -> bool {
    return c->get_last_cmd().data == cmd.data;
  };

  cluster->wait_leader_eletion();

  std::vector<std::shared_ptr<solidarity::raft>> leaders
      = cluster->by_filter(is_leader_pred);
  EXPECT_EQ(leaders.size(), size_t(1));

  for (int i = 0; i < 10; ++i) {
    cmd.data[0]++;
    auto st = leaders[0]->add_command(cmd);
    EXPECT_EQ(st, solidarity::ERROR_CODE::OK);
    while (true) {
      cluster->heartbeat();
      cluster->print_cluster();
      auto replicated_on = std::count_if(consumers.cbegin(), consumers.cend(), data_eq);
      if (size_t(replicated_on) == consumers.size()) {
        break;
      }
    }
  }

  for (size_t i = 0; i < new_nodes_count; ++i) {
    auto nname = "_" + std::to_string(i + 1 + exists_nodes_count);
    auto sett = solidarity::raft_settings_t().set_name(nname).set_election_timeout(et);
    auto state_machine = std::make_shared<mock_state_machine>();
    consumers.push_back(state_machine);
    auto cons = std::make_shared<raft>(
        sett, cluster.get(), memory_journal::make_new(), state_machine.get());
    cluster->add_new(sett.name(), cons);
    cluster->wait_leader_eletion();
  }

  while (true) {
    auto replicated_on = std::count_if(consumers.cbegin(), consumers.cend(), data_eq);
    if (size_t(replicated_on) == consumers.size()) {
      break;
    }
    solidarity::utils::logging::logger_info("[test] replicated_on: ", replicated_on);
    cluster->heartbeat();
    cluster->print_cluster();
  }

  cluster = nullptr;
  consumers.clear();
}

TEST_CASE("raft.log_compaction", "[raft]") {
  auto tst_log_prefix = solidarity::utils::strings::to_string("test?> ");
  auto tst_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
      solidarity::utils::logging::logger_manager::instance()->get_shared_logger(),
      tst_log_prefix);

  using solidarity::raft_settings_t;
  auto cluster = std::make_shared<mock_cluster>();

  size_t nodes_count = 4;
  size_t max_log_size = 3;
  std::vector<std::shared_ptr<mock_state_machine>> consumers;
  consumers.reserve(nodes_count);

  auto et = std::chrono::milliseconds(300);
  for (size_t i = 0; i < nodes_count; ++i) {
    auto nname = "_" + std::to_string(i);
    auto sett
        = raft_settings_t().set_name(nname).set_election_timeout(et).set_max_log_size(
            max_log_size);

    auto c = std::make_shared<mock_state_machine>();
    consumers.push_back(c);
    auto cons = std::make_shared<solidarity::raft>(
        sett, cluster.get(), solidarity::logdb::memory_journal::make_new(), c.get());
    cluster->add_new(sett.name(), cons);
  }

  cluster->wait_leader_eletion();

  solidarity::node_name last_leader;
  solidarity::command_t cmd(1);
  cmd.data[0] = 0;

  auto data_eq = [&cmd](const std::shared_ptr<mock_state_machine> &c) -> bool {
    return c->get_last_cmd().data == cmd.data;
  };

  std::vector<std::shared_ptr<solidarity::raft>> leaders;
  for (int i = 0; i < 10; ++i) {
    leaders = cluster->by_filter(is_leader_pred);
    if (leaders.size() != size_t(1)) {
      cluster->wait_leader_eletion();
    }
    cmd.data[0]++;
    auto st = leaders[0]->add_command(cmd);
    EXPECT_EQ(st, solidarity::ERROR_CODE::OK);
    while (true) {
      cluster->print_cluster();
      cluster->heartbeat();
      bool all_of = std::all_of(consumers.cbegin(), consumers.cend(), data_eq);
      if (all_of) {
        break;
      }
    }
  }

  auto all_nodes = cluster->get_all();
  std::vector<size_t> sizes;
  sizes.resize(all_nodes.size());
  while (true) {
    cluster->print_cluster();
    cluster->heartbeat();
    std::transform(all_nodes.cbegin(),
                   all_nodes.cend(),
                   sizes.begin(),
                   [](const std::shared_ptr<solidarity::raft> &c) {
                     return c->journal()->reccords_count();
                   });

    size_t count_of
        = std::count_if(sizes.cbegin(), sizes.cend(), [max_log_size](const size_t c) {
            return c <= max_log_size;
          });

    if (count_of == all_nodes.size()) {
      break;
    }
  }

  cluster = nullptr;
  consumers.clear();
}

bool operator==(const solidarity::logdb::log_entry &r,
                const solidarity::logdb::log_entry &l) {
  return r.term == l.term && r.kind == l.kind && r.cmd.size() == l.cmd.size()
         && std::equal(r.cmd.cbegin(), r.cmd.cend(), l.cmd.cbegin());
}

bool operator!=(const solidarity::logdb::log_entry &r,
                const solidarity::logdb::log_entry &l) {
  return !(r == l);
}

TEST_CASE("raft.apply_journal_on_start", "[raft]") {
  using solidarity::node_name;
  using solidarity::raft;
  using solidarity::logdb::memory_journal;

  auto cluster = std::make_shared<mock_cluster>();

  size_t exists_nodes_count = 1;
  std::vector<std::shared_ptr<mock_state_machine>> consumers;
  consumers.reserve(exists_nodes_count);

  auto et = std::chrono::milliseconds(300);
  auto jrn = memory_journal::make_new();

  solidarity::command_t cmd(1);
  cmd.data[0] = 0;

  for (int i = 0; i < 10; ++i) {
    cmd.data[0] += 2;
    solidarity::logdb::log_entry le;
    le.cmd = cmd;
    le.term = 1;
    auto ri = jrn->put(le);
    jrn->commit(ri.lsn);
  }

  auto nname = "_" + std::to_string(size_t(1));
  auto sett = solidarity::raft_settings_t().set_name(nname).set_election_timeout(et);
  auto state_machine = std::make_shared<mock_state_machine>();
  consumers.push_back(state_machine);
  auto cons = std::make_shared<raft>(sett, cluster.get(), jrn, state_machine.get());
  cluster->add_new(sett.name(), cons);

  EXPECT_EQ(state_machine->get_last_cmd().data, cmd.data);
}

TEST_CASE("raft.rollback", "[raft]") {
  using solidarity::node_name;
  using solidarity::raft;
  using solidarity::logdb::memory_journal;

  auto cluster = std::make_shared<mock_cluster>();

  const size_t exists_nodes_count = 2;
  std::vector<std::shared_ptr<mock_state_machine>> consumers;
  consumers.reserve(exists_nodes_count);

  auto et = std::chrono::milliseconds(300);
  solidarity::command_t cmd(1);

  std::shared_ptr<solidarity::raft> n1, n2;
  std::shared_ptr<solidarity::logdb::memory_journal> jrn1, jrn2;
  {
    auto nname = "_0";
    auto sett = solidarity::raft_settings_t().set_name(nname).set_election_timeout(et);
    auto state_machine = std::make_shared<mock_state_machine>();
    consumers.push_back(state_machine);
    jrn1 = memory_journal::make_new();

    solidarity::logdb::log_entry le;
    le.kind = solidarity::logdb::LOG_ENTRY_KIND::APPEND;
    le.cmd = cmd;

    for (size_t i = 0; i < 10; ++i) {
      le.cmd.data[0] = static_cast<uint8_t>(i);
      le.term = 1;
      jrn1->put(le);
    }
    n1 = std::make_shared<raft>(sett, cluster.get(), jrn1, state_machine.get());
    n1->rw_state().term = 1;
  }
  {
    auto nname = "_1";
    auto sett = solidarity::raft_settings_t().set_name(nname).set_election_timeout(et);
    auto state_machine = std::make_shared<mock_state_machine>();
    consumers.push_back(state_machine);
    jrn2 = memory_journal::make_new();

    solidarity::logdb::log_entry le;
    le.kind = solidarity::logdb::LOG_ENTRY_KIND::APPEND;
    le.cmd = cmd;
    for (size_t i = 0; i < 10; ++i) {
      le.cmd.data[0] = static_cast<uint8_t>(i);
      if (i >= 3) {
        le.term = 2;
      } else {
        le.term = 1;
      }
      jrn2->put(le);
    }
    jrn2->commit(jrn2->prev_rec().lsn);
    n2 = std::make_shared<raft>(sett, cluster.get(), jrn2, state_machine.get());
    n2->rw_state().term = 100500;
  }

  SECTION("from equal journal") { n2->rw_state().term = 100500; }
  SECTION("from big to small journal") {
    solidarity::logdb::log_entry le;
    le.kind = solidarity::logdb::LOG_ENTRY_KIND::APPEND;
    le.cmd = cmd;
    for (size_t i = 0; i < 20; ++i) {
      le.cmd.data[0] = static_cast<uint8_t>(i);
      le.term = n2->rw_state().term;
      jrn2->put(le);
    }
    jrn2->commit(jrn2->prev_rec().lsn);
  }

  SECTION("from small to big journal") {
    solidarity::logdb::log_entry le;
    le.kind = solidarity::logdb::LOG_ENTRY_KIND::APPEND;
    le.cmd = cmd;
    for (size_t i = 11; i < 15; ++i) {
      le.cmd.data[0] = static_cast<uint8_t>(i);
      le.term = 1;
      jrn1->put(le);
    }
    jrn1->commit(jrn1->prev_rec().lsn);
  }

  SECTION("rewrite all journal") {
    n2->rw_state().term = 100500;
    jrn1->erase_all_after(solidarity::index_t(-1));

    solidarity::logdb::log_entry le;
    le.kind = solidarity::logdb::LOG_ENTRY_KIND::APPEND;
    le.cmd = cmd;
    for (size_t i = 0; i < 2; ++i) {
      le.cmd.data[0] = static_cast<uint8_t>(i);
      le.term = 0;
      jrn1->put(le);
    }
  }
  EXPECT_FALSE(consumers.empty());
  cluster->add_new(n1->self_addr(), n1);
  cluster->add_new(n2->self_addr(), n2);

  cluster->wait_leader_eletion();
  cluster->print_cluster();
  auto leaders = cluster->by_filter(is_leader_pred);
  auto followers = cluster->by_filter(is_follower_pred);

  EXPECT_EQ(leaders.front()->self_addr(), n2->self_addr());
  EXPECT_EQ(followers.front()->self_addr(), n1->self_addr());

  while (true) {
    EXPECT_FALSE(consumers.empty());
    cluster->heartbeat();

    auto content1 = jrn1->dump();
    auto content2 = jrn2->dump();

    if (content1.size() == content2.size()) {
      bool contents_is_equal = true;
      for (const auto &kv : content1) {

        if (auto it = content2.find(kv.first); it == content2.end()) {
          contents_is_equal = false;
          break;
        } else {
          if (it->second != kv.second) {
            contents_is_equal = false;
            break;
          }
        }
      }
      if (contents_is_equal) {
        break;
      }
    }
  }

  cluster = nullptr;
  consumers.clear();
}

TEST_CASE("raft.can_apply", "[raft]") {
  using solidarity::node_name;
  using solidarity::raft;
  using solidarity::logdb::memory_journal;

  auto cluster = std::make_shared<mock_cluster>();

  size_t exists_nodes_count = 2;

  std::vector<std::shared_ptr<mock_state_machine>> consumers;
  consumers.reserve(exists_nodes_count);

  auto et = std::chrono::milliseconds(400);

  for (size_t i = 0; i < exists_nodes_count; ++i) {
    auto nname = "_" + std::to_string(i);
    auto sett = solidarity::raft_settings_t().set_name(nname).set_election_timeout(et);
    auto state_machine = std::make_shared<mock_state_machine>(false);
    consumers.push_back(state_machine);
    auto cons = std::make_shared<raft>(
        sett, cluster.get(), memory_journal::make_new(), state_machine.get());
    cluster->add_new(sett.name(), cons);
  }
  solidarity::command_t cmd(1);
  cmd.data[0] = 0;

  auto data_eq = [&cmd](const std::shared_ptr<mock_state_machine> &c) -> bool {
    return c->get_last_cmd().data == cmd.data;
  };

  cluster->wait_leader_eletion();

  std::vector<std::shared_ptr<solidarity::raft>> leaders
      = cluster->by_filter(is_leader_pred);
  EXPECT_EQ(leaders.size(), size_t(1));

  cmd.data[0]++;
  auto st = leaders[0]->add_command(cmd);

  EXPECT_EQ(st, solidarity::ERROR_CODE::STATE_MACHINE_CAN_T_APPLY_CMD);

  for (auto &c : consumers) {
    c->set_can_apply(true);
  }

  st = leaders[0]->add_command(cmd);
  EXPECT_EQ(st, solidarity::ERROR_CODE::OK);
  while (true) {
    cluster->heartbeat();
    cluster->print_cluster();
    auto replicated_on = std::count_if(consumers.cbegin(), consumers.cend(), data_eq);
    if (size_t(replicated_on) == consumers.size()) {
      break;
    }
  }

  cluster = nullptr;
  consumers.clear();
}
