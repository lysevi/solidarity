#include "helpers.h"
#include <solidarity/node.h>
#include <solidarity/special/licenseservice.h>
#include <solidarity/special/lockservice.h>
#include <solidarity/utils/logger.h>

#include <algorithm>
#include <catch.hpp>
#include <condition_variable>
#include <iostream>
#include <iterator>
#include <numeric>

TEST_CASE("lockservice", "[special]") {
  size_t cluster_size = 2;
  auto tst_log_prefix = solidarity::utils::strings::to_string("test?> ");
  auto tst_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
      solidarity::utils::logging::logger_manager::instance()->get_shared_logger(),
      tst_log_prefix);

  std::vector<unsigned short> ports(cluster_size);
  std::iota(ports.begin(), ports.end(), (unsigned short)(8000));

  std::unordered_map<std::string, std::shared_ptr<solidarity::node>> nodes;
  std::unordered_map<std::string, std::shared_ptr<solidarity::special::lockservice>>
      consumers;

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

    auto state_machine = std::make_shared<solidarity::special::lockservice>();
    auto n = std::make_shared<solidarity::node>(node_logger, params, state_machine.get());

    n->start();
    n->add_event_handler([](const solidarity::client_event_t &ev) {
      if (ev.kind == solidarity::client_event_t::event_kind::COMMAND_STATUS) {
        std::stringstream ss;
        ss << "command " << solidarity::to_string(ev);
        std::cerr << ss.str() << std::endl;
      }
    });

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

  for (auto &kv : nodes) {
    auto c = clients[kv.first];
    solidarity::special::lockservice_client lc(kv.first, c);
    std::cout << "try " << kv.first << std::endl;
    lc.lock(kv.first);
  }

  auto tr = std::thread([clients]() {
    auto kv = clients.begin();
    auto c = kv->second;
    solidarity::special::lockservice_client lc(kv->first, c);
    ++kv;
    lc.lock(kv->first);
  });

  for (auto &kv : nodes) {
    auto c = clients[kv.first];
    solidarity::special::lockservice_client lc(kv.first, c);
    std::cout << "try unlock" << kv.first << std::endl;
    lc.unlock(kv.first);
  }

  tr.join();

  for (auto &kv : nodes) {
    std::cerr << "stop node " << kv.first << std::endl;

    kv.second->stop();
  }
  consumers.clear();
  clients.clear();
}

TEST_CASE("licenseservice", "[special]") {
  size_t cluster_size = 2;
  auto tst_log_prefix = solidarity::utils::strings::to_string("test?> ");
  auto tst_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
      solidarity::utils::logging::logger_manager::instance()->get_shared_logger(),
      tst_log_prefix);

  std::vector<unsigned short> ports(cluster_size);
  std::iota(ports.begin(), ports.end(), (unsigned short)(8000));

  std::unordered_map<std::string, size_t> lics = {{"l1", 2}, {"l2", 2}, {"l3", 2}};
  std::unordered_map<std::string, std::shared_ptr<solidarity::node>> nodes;
  std::unordered_map<std::string, std::shared_ptr<solidarity::special::licenseservice>>
      consumers;

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

    auto state_machine = std::make_shared<solidarity::special::licenseservice>(lics);
    auto n = std::make_shared<solidarity::node>(node_logger, params, state_machine.get());

    n->start();
    n->add_event_handler([](const solidarity::client_event_t &ev) {
      if (ev.kind == solidarity::client_event_t::event_kind::COMMAND_STATUS) {
        std::stringstream ss;
        ss << "command " << solidarity::to_string(ev);
        std::cerr << ss.str() << std::endl;
      }
    });

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

  for (int i = 0; i < 2; ++i) {
    /// lock
    for (auto &kv : nodes) {
      auto c = clients[kv.first];
      solidarity::special::licenseservice_client lc(kv.first, c);
      for (auto lkv : lics) {
        std::cout << "try " << lkv.first << std::endl;
        while (true) {
          auto r = lc.try_lock(lkv.first);
          if (r) {
            break;
          }
        }
      }
    }

    /// read
    for (auto &kv : nodes) {
      auto c = clients[kv.first];
      solidarity::special::licenseservice_client lc(kv.first, c);
      for (auto lkv : lics) {
        std::cout << "read lockers " << lkv.first << std::endl;
        auto r = lc.lockers(lkv.first);
        std::cout << "readed ";
        std::copy(
            r.begin(), r.end(), std::ostream_iterator<std::string>(std::cout, ", "));
        std::cout << std::endl;

        EXPECT_EQ(r.size(), nodes.size());
      }
    }

    /// lock2
    for (auto &kv : nodes) {
      auto c = clients[kv.first];
      solidarity::special::licenseservice_client lc(kv.first, c);
      for (auto lkv : lics) {
        std::cout << "try " << lkv.first << std::endl;
        auto r = lc.try_lock(lkv.first);
        EXPECT_FALSE(r);
      }
    }

    /// unlock
    for (auto &kv : nodes) {
      auto c = clients[kv.first];
      solidarity::special::licenseservice_client lc(kv.first, c);
      for (auto lkv : lics) {
        std::cout << "unlock " << lkv.first << std::endl;
        lc.unlock(lkv.first);
      }
    }

    /// read
    for (auto &kv : nodes) {
      auto c = clients[kv.first];
      solidarity::special::licenseservice_client lc(kv.first, c);
      for (auto lkv : lics) {
        std::cout << "read lockers " << lkv.first << std::endl;
        while (true) {
          auto r = lc.lockers(lkv.first);
          if (r.empty()) {
            break;
          }
        }
      }
    }
  }
}
