#include "helpers.h"
#include <librft/client.h>
#include <librft/node.h>
#include <libutils/logger.h>
#include <libutils/strings.h>

#include "mock_consumer.h"

#include <catch.hpp>

TEST_CASE("node", "[network]") {
  size_t cluster_size = 0;
  auto tst_log_prefix = utils::strings::args_to_string("test?> ");
  auto tst_logger = std::make_shared<utils::logging::prefix_logger>(
      utils::logging::logger_manager::instance()->get_logger(), tst_log_prefix);

  SECTION("node.2") { cluster_size = 2; }
  SECTION("node.4") { cluster_size = 4; }

  std::vector<unsigned short> ports(cluster_size);
  std::iota(ports.begin(), ports.end(), unsigned short(8000));

  std::unordered_map<std::string, std::shared_ptr<rft::node>> nodes;
  std::unordered_map<std::string, std::shared_ptr<mock_consumer>> consumers;

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
    params.client_port = (unsigned short)10000;
    params.thread_count = 1;
    params.cluster = out_addrs;
    params.name = utils::strings::args_to_string("node_", p);

    auto consumer = std::make_shared<mock_consumer>();
    auto n = std::make_shared<rft::node>(params, consumer.get());

    consumers[params.name] = consumer;
    nodes[params.name] = n;

    n->start();
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

  for (auto &kv : nodes) {
    kv.second->stop();
  }
}
