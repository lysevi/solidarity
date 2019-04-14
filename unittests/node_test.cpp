#include "helpers.h"
#include <libsolidarity/client.h>
#include <libsolidarity/node.h>
#include <libsolidarity/utils/logger.h>
#include <libsolidarity/utils/strings.h>

#include "mock_state_machine.h"

#include <catch.hpp>
#include <condition_variable>
#include <numeric>
#include <iostream>

TEST_CASE("node", "[network]") {
  size_t cluster_size = 0;
  auto tst_log_prefix = solidarity::utils::strings::args_to_string("test?> ");
  auto tst_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
      solidarity::utils::logging::logger_manager::instance()->get_shared_logger(), tst_log_prefix);

  SECTION("node.2") { cluster_size = 2; }
  SECTION("node.3") { cluster_size = 3; }
  SECTION("node.5") { cluster_size = 5; }

  std::vector<unsigned short> ports(cluster_size);
  std::iota(ports.begin(), ports.end(), unsigned short(8000));

  std::unordered_map<std::string, std::shared_ptr<solidarity::node>> nodes;
  std::unordered_map<std::string, std::shared_ptr<mock_state_machine>> consumers;
  std::unordered_map<std::string, std::shared_ptr<solidarity::client>> clients;

  std::cerr << "start nodes" << std::endl;
  unsigned short client_port = 10000;
  for (auto p : ports) {
    std::vector<unsigned short> out_ports;
    out_ports.reserve(ports.size() - 1);
    std::copy_if(ports.begin(),
                 ports.end(),
                 std::back_inserter(out_ports),
                 [p](const auto v) { return v != p; });

    EXPECT_EQ(out_ports.size(), ports.size() - 1);

    std::vector<std::string> out_addrs;
    out_addrs.reserve(out_ports.size());
    std::transform(
        out_ports.begin(),
        out_ports.end(),
        std::back_inserter(out_addrs),
        [](const auto prt) { return solidarity::utils::strings::args_to_string("localhost:", prt); });

    solidarity::node::params_t params;
    params.port = p;
    params.client_port = client_port++;
    params.thread_count = 1;
    params.cluster = out_addrs;
    params.name = solidarity::utils::strings::args_to_string("node_", p);
    std::cerr << params.name << " starting..." << std::endl;
    auto log_prefix = solidarity::utils::strings::args_to_string(params.name, "> ");
    auto node_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
        solidarity::utils::logging::logger_manager::instance()->get_shared_logger(), log_prefix);

    auto state_machine = std::make_shared<mock_state_machine>();
    auto n = std::make_shared<solidarity::node>(node_logger, params, state_machine.get());

    n->start();

    solidarity::client::params_t cpar(
        solidarity::utils::strings::args_to_string("client_", params.name));
    cpar.threads_count = 1;
    cpar.host = "localhost";
    cpar.port = params.client_port;

    auto c = std::make_shared<solidarity::client>(cpar);
    c->connect();

    while (!c->is_connected()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_EQ(n->connections_count(), size_t(1));

    consumers[params.name] = state_machine;
    nodes[params.name] = n;
    clients[params.name] = c;
  }

  std::cerr << "wait election" << std::endl;
  std::unordered_set<solidarity::node_name> leaders;
  while (true) {
    leaders.clear();
    for (auto &kv : nodes) {
      if (kv.second->state().node_kind == solidarity::NODE_KIND::LEADER) {
        leaders.insert(kv.second->self_name());
      }
    }
    if (leaders.size() == 1) {
      auto leader_name = *leaders.begin();
      bool election_complete = true;
      for (auto &kv : nodes) {
        auto state = kv.second->state();
        auto nkind = state.node_kind;
        if ((nkind == solidarity::NODE_KIND::LEADER
             || nkind == solidarity::NODE_KIND::FOLLOWER)
            && state.leader.name() != leader_name.name()) {
          election_complete = false;
          break;
        }
      }
      if (election_complete) {
        break;
      }
    }
  }

  std::cerr << "send over leader" << std::endl;
  auto leader_name = leaders.begin()->name();
  // std::shared_ptr<solidarity::node> leader_node = nodes[leader_name];

  auto leader_client = clients[leader_name];
  std::vector<uint8_t> first_cmd{1, 2, 3, 4, 5};

  std::mutex locker;
  std::unique_lock ulock(locker);
  bool is_on_update_received = false;
  std::condition_variable cond;

  auto uh_id = leader_client->add_update_handler([&is_on_update_received, &cond]() {
    is_on_update_received = true;
    cond.notify_all();
  });

  {
    auto ecode = leader_client->send(first_cmd);
    EXPECT_EQ(ecode, solidarity::ERROR_CODE::OK);
  }

  while (true) {
    cond.wait(ulock, [&is_on_update_received]() { return is_on_update_received; });
    if (is_on_update_received) {
      break;
    }
  }

  leader_client->rm_update_handler(uh_id);
  std::cerr << "resend test" << std::endl;
  tst_logger->info("resend test");

  for (auto &kv : clients) {
    auto suffix = leader_name == kv.first ? " is a leader" : "";
    std::cerr << "resend over " << kv.first << suffix << std::endl;
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

    solidarity::ERROR_CODE send_ecode = solidarity::ERROR_CODE::UNDEFINED;
    int i = 0;
    do {
      tst_logger->info("try resend cmd. step #", i++);
      send_ecode = kv.second->send(first_cmd);
    } while (send_ecode != solidarity::ERROR_CODE::OK);

    auto expected_answer = first_cmd;
    std::transform(expected_answer.begin(),
                   expected_answer.end(),
                   expected_answer.begin(),
                   [](auto &v) -> uint8_t { return uint8_t(v + 1); });
    while (true) {
      auto answer = kv.second->read({1});
      if (std::equal(expected_answer.begin(),
                     expected_answer.end(),
                     answer.begin(),
                     answer.end())) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  for (auto &kv : nodes) {
    std::cerr << "stop node " << kv.first << std::endl;
    auto client = clients[kv.first];

    solidarity::ERROR_CODE c = solidarity::ERROR_CODE::OK;
    auto id = client->add_client_event_handler(
        [&c](const solidarity::client_state_event_t &ev) mutable { c = ev.ecode; });
    kv.second->stop();

    while (c != solidarity::ERROR_CODE::NETWORK_ERROR) {
      std::this_thread::yield();
    }
    client->rm_client_event_handler(id);
  }

  for (auto &kv : clients) {
    while (kv.second->is_connected()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}
