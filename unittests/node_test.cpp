#include "helpers.h"
#include <librft/client.h>
#include <librft/node.h>
#include <libutils/logger.h>
#include <libutils/strings.h>

#include "mock_consumer.h"

#include <catch.hpp>
#include <condition_variable>

TEST_CASE("node", "[network]") {
  size_t cluster_size = 0;
  auto tst_log_prefix = utils::strings::args_to_string("test?> ");
  auto tst_logger = std::make_shared<utils::logging::prefix_logger>(
      utils::logging::logger_manager::instance()->get_shared_logger(), tst_log_prefix);

  SECTION("node.2") { cluster_size = 2; }
  SECTION("node.3") { cluster_size = 3; }
  SECTION("node.5") { cluster_size = 5; }

  std::vector<unsigned short> ports(cluster_size);
  std::iota(ports.begin(), ports.end(), unsigned short(8000));

  std::unordered_map<std::string, std::shared_ptr<rft::node>> nodes;
  std::unordered_map<std::string, std::shared_ptr<mock_consumer>> consumers;
  std::unordered_map<std::string, std::shared_ptr<rft::client>> clients;

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
        [](const auto prt) { return utils::strings::args_to_string("localhost:", prt); });

    rft::node::params_t params;
    params.port = p;
    params.client_port = client_port++;
    params.thread_count = 1;
    params.cluster = out_addrs;
    params.name = utils::strings::args_to_string("node_", p);

    auto log_prefix = utils::strings::args_to_string(params.name, "> ");
    auto node_logger = std::make_shared<utils::logging::prefix_logger>(
        utils::logging::logger_manager::instance()->get_shared_logger(), log_prefix);

    auto consumer = std::make_shared<mock_consumer>();
    auto n = std::make_shared<rft::node>(node_logger, params, consumer.get());

    n->start();

    rft::client::params_t cpar(utils::strings::args_to_string("client_", params.name));
    cpar.threads_count = 1;
    cpar.host = "localhost";
    cpar.port = params.client_port;

    auto c = std::make_shared<rft::client>(cpar);
    c->connect();

    while (!c->is_connected()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_EQ(n->connections_count(), size_t(1));

    consumers[params.name] = consumer;
    nodes[params.name] = n;
    clients[params.name] = c;
  }

  std::unordered_set<rft::cluster_node> leaders;
  while (true) {
    leaders.clear();
    for (auto &kv : nodes) {
      if (kv.second->state().node_kind == rft::NODE_KIND::LEADER) {
        leaders.insert(kv.second->self_name());
      }
    }
    if (leaders.size() == 1) {
      auto leader_name = *leaders.begin();
      bool election_complete = true;
      for (auto &kv : nodes) {
        auto state = kv.second->state();
        auto nkind = state.node_kind;
        if ((nkind == rft::NODE_KIND::LEADER || nkind == rft::NODE_KIND::FOLLOWER)
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
  // std::shared_ptr<rft::node> leader_node = nodes[leader_name];

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

  leader_client->send(first_cmd);

  while (true) {
    cond.wait(ulock, [&is_on_update_received]() { return is_on_update_received; });
    if (is_on_update_received) {
      break;
    }
  }

  leader_client->rm_update_handler(uh_id);

  for (auto &kv : clients) {
    std::transform(first_cmd.begin(),
                   first_cmd.end(),
                   first_cmd.begin(),
                   [](auto &v) -> uint8_t { return uint8_t(v + 1); });
    kv.second->send(first_cmd);

    auto target = first_cmd;
    std::transform(target.begin(), target.end(), target.begin(), [](auto &v) -> uint8_t {
      return uint8_t(v + 1);
    });
    while (true) {
      auto answer = kv.second->read({1});
      if (std::equal(target.begin(), target.end(), answer.begin(), answer.end())) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  for (auto &kv : nodes) {
    kv.second->stop();
  }

  for (auto &kv : clients) {
    while (kv.second->is_connected()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}
