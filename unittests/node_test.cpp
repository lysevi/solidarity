#include "helpers.h"
#include <solidarity/client.h>
#include <solidarity/dialler/message.h>
#include <solidarity/node.h>
#include <solidarity/utils/logger.h>
#include <solidarity/utils/strings.h>

#include "mock_state_machine.h"
#include "test_description_t.h"

#include <catch.hpp>
#include <condition_variable>
#include <iostream>
#include <numeric>

class node_test_description_t : public test_description_t {
public:
  node_test_description_t()
      : test_description_t() {}

  std::unordered_map<uint32_t, std::shared_ptr<solidarity::abstract_state_machine>>
  get_state_machines() override {
    std::unordered_map<uint32_t, std::shared_ptr<solidarity::abstract_state_machine>>
        result;
    result[uint32_t(0)] = std::make_shared<mock_state_machine>();
    return result;
  }
};

TEST_CASE("node", "[network]") {
  size_t cluster_size = 0;
  size_t large_cmd_size = size_t(solidarity::dialler::message::MAX_BUFFER_SIZE * 3.75);
  size_t cmd_size = 5;
  auto tst_log_prefix = solidarity::utils::strings::to_string("test?> ");
  auto tst_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
      solidarity::utils::logging::logger_manager::instance()->get_shared_logger(),
      tst_log_prefix);

  SECTION("node.2") {
    SECTION("small data") {}
    SECTION("large data.x3.75") { cmd_size = large_cmd_size; }
    cluster_size = 2;
  }
  SECTION("node.3") {
    SECTION("small data") {}
    SECTION("large data.x3.75") { cmd_size = large_cmd_size; }
    cluster_size = 3;
  }
  SECTION("node.5") {
    SECTION("small data") {}
    SECTION("large data.x3.75") { cmd_size = large_cmd_size; }
    cluster_size = 5;
  }
  node_test_description_t td;
  td.init(cluster_size);

  std::unordered_set<solidarity::node_name> leaders = td.wait_election();

  auto leader_name = *leaders.begin();
  std::cerr << "send over leader " << leader_name << std::endl;
  tst_logger->info("send over leader ", leader_name);

  auto leader_client = td.clients[leader_name];
  std::vector<uint8_t> first_cmd(cmd_size);
  std::iota(first_cmd.begin(), first_cmd.end(), uint8_t(0));

  std::mutex locker;
  std::unique_lock ulock(locker);
  std::atomic_bool is_on_update_received = false;
  std::condition_variable cond;

  auto uh_id = leader_client->add_event_handler([&is_on_update_received, &cond](auto) {
    is_on_update_received = true;
    cond.notify_all();
  });

  {
    auto ecode = leader_client->send_weak(first_cmd);
    std::cerr << "leader_client->send_weak: ecode - " << solidarity::to_string(ecode)
              << std::endl;
    EXPECT_EQ(ecode, solidarity::ERROR_CODE::OK);
  }

  while (true) {
    cond.wait(ulock, [&is_on_update_received]() { return is_on_update_received.load(); });
    if (is_on_update_received) {
      break;
    }
  }

  leader_client->rm_event_handler(uh_id);
  std::cerr << "resend test" << std::endl;
  tst_logger->info("resend test");

  for (auto &kv : td.clients) {
    bool is_a_leader = leader_name == kv.first;
    auto suffix = is_a_leader ? " is a leader" : "";
    std::cerr << "resend over " << kv.first << suffix << std::endl;
    tst_logger->info("resend over ", kv.first, suffix);
    std::transform(first_cmd.begin(),
                   first_cmd.end(),
                   first_cmd.begin(),
                   [](auto &v) -> uint8_t { return uint8_t(v + 1); });
    {
      std::ostringstream oss;
      oss << "[";
      std::copy(first_cmd.begin(), first_cmd.end(), std::ostream_iterator<int>(oss, ","));
      oss << "]";
      tst_logger->info("send over ", kv.first, " cmd:", oss.str());
    }

    std::atomic_bool is_state_changed = false;
    auto client_handler_id = kv.second->add_event_handler(
        [&is_state_changed, kv](const solidarity::client_event_t &rse) mutable {
          std::cerr << kv.first << ": " << solidarity::to_string(rse) << std::endl;
          if (rse.kind == solidarity::client_event_t::event_kind::RAFT) {
            is_state_changed = true;
          }
        });

    solidarity::ERROR_CODE send_ecode = solidarity::ERROR_CODE::UNDEFINED;
    int i = 0;
    std::cerr << "resend cycle" << std::endl;
    do {
      tst_logger->info("try resend cmd. step #", i++);
      auto sst = kv.second->send_strong(first_cmd);
      if (is_state_changed || sst.is_ok()) {
        break;
      }
      send_ecode = sst.ecode;
    } while (send_ecode != solidarity::ERROR_CODE::OK);

    auto expected_answer = first_cmd;
    std::transform(expected_answer.begin(),
                   expected_answer.end(),
                   expected_answer.begin(),
                   [](auto &v) -> uint8_t { return uint8_t(v + 1); });
    std::cerr << "wait command" << std::endl;
    while (!is_state_changed) {
      auto answer = kv.second->read({1});
      if (std::equal(expected_answer.begin(),
                     expected_answer.end(),
                     answer.begin(),
                     answer.end())) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    /// write over node
    std::cerr << "write over node " << kv.first << suffix << std::endl;
    std::transform(first_cmd.begin(),
                   first_cmd.end(),
                   first_cmd.begin(),
                   [](auto &v) -> uint8_t { return uint8_t(v + 1); });

    send_ecode = solidarity::ERROR_CODE::UNDEFINED;
    solidarity::command_t cmd;
    auto tnode = td.nodes[kv.first];
    std::atomic_bool writed_to_node_sm = false;
    uint64_t node_handler_id = tnode->add_event_handler(
        [&writed_to_node_sm](const solidarity::client_event_t &ev) {
          if (ev.kind == solidarity::client_event_t::event_kind::COMMAND_STATUS) {
            auto cs = ev.cmd_ev.value();

            std::stringstream ss;
            ss << "command status - crc=" << cs.crc
               << " status=" << solidarity::to_string(cs.status) << std::endl;

            std::cerr << ss.str();
            if (cs.status == solidarity::command_status::WAS_APPLIED) {
              writed_to_node_sm = true;
            }
          }
        });

    i = 0;
    do {
      i++;
      tst_logger->info("try write cmd. step #", i);
      cmd.data = first_cmd;

      send_ecode = tnode->add_command(cmd);
      if (send_ecode != solidarity::ERROR_CODE::OK) {
        // EXPECT_FALSE(is_a_leader);

        tst_logger->info("try resend cmd. step #", i);
        std::cerr << "try resend cmd. step #" << i << std::endl;
        auto ares = tnode->add_command_to_cluster(cmd);
        ares->wait();
        send_ecode = ares->ecode();
      }
      if (is_state_changed) {
        break;
      }
    } while (send_ecode != solidarity::ERROR_CODE::OK);

    std::transform(expected_answer.begin(),
                   expected_answer.end(),
                   expected_answer.begin(),
                   [](auto &v) -> uint8_t { return uint8_t(v + 1); });

    while (!is_state_changed) {
      auto answer = kv.second->read({1});
      if (std::equal(expected_answer.begin(),
                     expected_answer.end(),
                     answer.begin(),
                     answer.end())) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    while (!writed_to_node_sm) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    tnode->rm_event_handler(node_handler_id);
    kv.second->rm_event_handler(client_handler_id);
  }

  // cluster state
  for (auto &kv : td.nodes) {
    auto tnode = td.nodes[kv.first];
    auto cst = tnode->cluster_status();
    EXPECT_FALSE(cst.leader.empty());
    EXPECT_EQ(cst.state.size(), cluster_size);
    for (auto &skv : cst.state) {
      EXPECT_FALSE(skv.first.empty());
    }
  }

  for (auto &kv : td.nodes) {
    std::cerr << "stop node " << kv.first << std::endl;
    auto client = td.clients[kv.first];

    std::mutex c_locker;
    solidarity::ERROR_CODE c = solidarity::ERROR_CODE::OK;
    auto id = client->add_event_handler(
        [&c, &c_locker](const solidarity::client_event_t &ev) mutable {
          if (ev.kind == solidarity::client_event_t::event_kind::NETWORK) {
            std::lock_guard l(c_locker);
            auto nse = ev.net_ev.value();
            c = nse.ecode;
          }
        });
    kv.second->stop();

    while (true) {
      {
        std::lock_guard l(c_locker);
        if (c == solidarity::ERROR_CODE::NETWORK_ERROR) {
          break;
        }
      }
      std::this_thread::yield();
    }
    client->rm_event_handler(id);
  }

  for (auto &kv : td.clients) {
    while (kv.second->is_connected()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  td.nodes.clear();
  td.clients.clear();
  td.consumers.clear();
}

class node_msm_test_description_t : public test_description_t {
public:
  node_msm_test_description_t()
      : test_description_t() {}

  std::unordered_map<uint32_t, std::shared_ptr<solidarity::abstract_state_machine>>
  get_state_machines() override {
    std::unordered_map<uint32_t, std::shared_ptr<solidarity::abstract_state_machine>>
        result;
    result[uint32_t(0)] = std::make_shared<mock_state_machine>();
    result[uint32_t(1)] = std::make_shared<mock_state_machine>();
    return result;
  }
};

TEST_CASE("node.multi_asm", "[network]") {
  size_t cluster_size = 3;
  auto tst_log_prefix = solidarity::utils::strings::to_string("test?> ");
  auto tst_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
      solidarity::utils::logging::logger_manager::instance()->get_shared_logger(),
      tst_log_prefix);

  node_msm_test_description_t td;
  td.init(cluster_size);

  std::unordered_set<solidarity::node_name> leaders = td.wait_election();

  size_t i = 0;
  tst_logger->info("send to 0");
  for (auto &kv : td.clients) {
    auto cmd = solidarity::command_t::from_value(i);
    cmd.asm_num = 0;
    solidarity::send_result sres;
    do {
      sres = kv.second->send_strong(cmd);
    } while (!sres.is_ok());

    auto asm_ptr
        = std::dynamic_pointer_cast<mock_state_machine>(td.consumers[kv.first][0]);
    while (!asm_ptr->get_last_cmd().is_empty()
           && asm_ptr->get_last_cmd().to_value<size_t>() != i) {
    }

    auto other_asm__ptr
        = std::dynamic_pointer_cast<mock_state_machine>(td.consumers[kv.first][1]);
    EXPECT_TRUE(other_asm__ptr->get_last_cmd().is_empty());
    i++;
  }

  tst_logger->info("send to 1");
  for (auto &kv : td.clients) {
    auto cmd = solidarity::command_t::from_value(i);
    cmd.asm_num = 1;
    solidarity::send_result sres;
    do {
      sres = kv.second->send_strong(cmd);
    } while (!sres.is_ok());

    auto asm_ptr
        = std::dynamic_pointer_cast<mock_state_machine>(td.consumers[kv.first][1]);
    while (!asm_ptr->get_last_cmd().is_empty()
           && asm_ptr->get_last_cmd().to_value<size_t>() != i) {
    }

    auto other_asm__ptr
        = std::dynamic_pointer_cast<mock_state_machine>(td.consumers[kv.first][0]);
    EXPECT_TRUE(other_asm__ptr->get_last_cmd().to_value<size_t>() != i);
  }

  td.nodes.clear();
  td.clients.clear();
  td.consumers.clear();
}
