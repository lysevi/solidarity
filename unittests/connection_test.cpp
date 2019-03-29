#include "helpers.h"
#include <librft/connection.h>
#include <catch.hpp>

TEST_CASE("connection", "[network]") {
  size_t cluster_size = 0;
  SECTION("connection.2") { cluster_size = 2; }
  SECTION("connection.3") { cluster_size = 3; }
  SECTION("connection.5") { cluster_size = 5; }

  std::vector<unsigned short> ports(cluster_size);
  std::iota(ports.begin(), ports.end(), unsigned short(8000));

  std::vector<std::shared_ptr<rft::cluster_connection>> cons;
  cons.reserve(cluster_size);

  for (auto p : ports) {
    std::vector<unsigned short> out_ports;
    out_ports.reserve(ports.size() - 1);
    std::copy_if(ports.begin(),
                 ports.end(),
                 std::back_inserter(out_ports),
                 [p](const auto v) { return v != p; });

    EXPECT_EQ(out_ports.size(), ports.size() - 1);

    rft::cluster_connection::params_t params;
    params.listener_params.port = p;
    params.addrs.reserve(out_ports.size());
    std::transform(
        out_ports.begin(),
        out_ports.end(),
        std::back_inserter(params.addrs),
        [](const auto prt) { return dialler::dial::params_t("localhost", prt); });

    auto log_prefix = utils::strings::args_to_string("localhost_", p, ": ");
    auto logger = std::make_shared<utils::logging::prefix_logger>(
        utils::logging::logger_manager::instance()->get_logger(), log_prefix);

    auto addr = rft::cluster_node().set_name(utils::strings::args_to_string("node_", p));
    auto c = std::make_shared<rft::cluster_connection>(addr, logger, params);
    cons.push_back(c);
    c->start();
  }

  for (auto &v : cons) {

    while (true) {
      auto nds = v->all_nodes();
      utils::logging::logger_info("wait node: ", v->self_addr(), " --> ", nds.size());

      if (nds.size() == cluster_size - 1) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
  }
  for (auto &&v : cons) {
    v->stop();
  }
  cons.clear();
}