#include "helpers.h"
#include <solidarity/client.h>
#include <solidarity/dialler/message.h>
#include <solidarity/node.h>
#include <solidarity/utils/logger.h>
#include <solidarity/utils/strings.h>

#include "mock_state_machine.h"

#include <catch.hpp>
#include <condition_variable>
#include <iostream>
#include <numeric>

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

  std::vector<unsigned short> ports(cluster_size);
  std::iota(ports.begin(), ports.end(), (unsigned short)(8000));

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
    std::transform(out_ports.begin(),
                   out_ports.end(),
                   std::back_inserter(out_addrs),
                   [](const auto prt) {
                     return solidarity::utils::strings::to_string("localhost:", prt);
                   });

    solidarity::node::params_t params;
    params.port = p;
    params.client_port = client_port++;
    params.thread_count = 1;
    params.cluster = out_addrs;
    params.name = solidarity::utils::strings::to_string("node_", p);
    std::cerr << params.name << " starting..." << std::endl;
    auto log_prefix = solidarity::utils::strings::to_string(params.name, "> ");
    auto node_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
        solidarity::utils::logging::logger_manager::instance()->get_shared_logger(),
        log_prefix);

    auto state_machine = std::make_shared<mock_state_machine>();
    auto n = std::make_shared<solidarity::node>(node_logger, params, state_machine.get());

    n->start();

    solidarity::client::params_t cpar(
        solidarity::utils::strings::to_string("client_", params.name));
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
        if (nkind != solidarity::NODE_KIND::LEADER
            && nkind != solidarity::NODE_KIND::FOLLOWER) {
          election_complete = false;
          break;
        }
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

  auto leader_name = leaders.begin()->name();
  std::cerr << "send over leader " << leader_name << std::endl;
  tst_logger->info("send over leader ", leader_name);

  auto leader_client = clients[leader_name];
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

  for (auto &kv : clients) {
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
    solidarity::command cmd;
    auto tnode = nodes[kv.first];
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
        //EXPECT_FALSE(is_a_leader);

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

  for (auto &kv : nodes) {
    std::cerr << "stop node " << kv.first << std::endl;
    auto client = clients[kv.first];

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

  for (auto &kv : clients) {
    while (kv.second->is_connected()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  nodes.clear();
  clients.clear();
  consumers.clear();
}
