#include "helpers.h"
#include "mock_state_machine.h"
#include <catch.hpp>
#include <solidarity/mesh_connection.h>
#include <numeric>

struct mock_cluster_client : solidarity::abstract_cluster_client {
  void recv(const solidarity::node_name &, const solidarity::append_entries &e) override {
    std::lock_guard l(locker);
    data = e.cmd.data;
  }

  void lost_connection_with(const solidarity::node_name &addr) override {
    std::lock_guard l(locker);
    lost_con.insert(addr);
  }

  void new_connection_with(const solidarity::node_name &addr) override {
    std::lock_guard l(locker);
    connected.insert(addr);
  }

  void heartbeat() override {}

  bool data_equal_to(const std::vector<std::uint8_t> &o) const {
    std::lock_guard l(locker);
    return std::equal(data.cbegin(), data.cend(), o.cbegin(), o.cend());
  }

  bool is_connection_lost(const solidarity::node_name &addr) const {
    std::lock_guard l(locker);
    return lost_con.find(addr) != lost_con.end();
  }

  size_t connections_size() const {
    std::lock_guard l(locker);
    return connected.size();
  }

  solidarity::ERROR_CODE add_command(const solidarity::command &) override {
    return solidarity::ERROR_CODE::OK;
  }

  std::vector<std::uint8_t> data;
  mutable std::mutex locker;
  std::unordered_set<solidarity::node_name> lost_con;
  std::unordered_set<solidarity::node_name> connected;
};

TEST_CASE("mesh_connection", "[network]") {
  size_t cluster_size = 0;
  auto tst_log_prefix = solidarity::utils::strings::args_to_string("test?> ");
  auto tst_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
      solidarity::utils::logging::logger_manager::instance()->get_shared_logger(),
      tst_log_prefix);
  size_t data_size = 1;

  SECTION("mesh_connection.2") {
    cluster_size = 2;
    SECTION("mesh_connection.small_data") { data_size = 1; }
    SECTION("mesh_connection.big_data") {
      data_size
          = size_t(solidarity::dialler::message::MAX_BUFFER_SIZE * cluster_size * 2);
    }
  }

  SECTION("mesh_connection.3") {
    cluster_size = 3;
    SECTION("mesh_connection.small_data") { data_size = 1; }
    SECTION("mesh_connection.big_data") {
      data_size
          = size_t(solidarity::dialler::message::MAX_BUFFER_SIZE * cluster_size * 3.75);
    }
  }
  SECTION("mesh_connection.5") {
    cluster_size = 5;
    SECTION("mesh_connection.small_data") { data_size = 1; }
    SECTION("mesh_connection.big_data") {
      data_size
          = size_t(solidarity::dialler::message::MAX_BUFFER_SIZE * cluster_size * 1.75);
    }
  }

  std::vector<unsigned short> ports(cluster_size);
  std::iota(ports.begin(), ports.end(), unsigned short(8000));

  std::vector<std::shared_ptr<solidarity::mesh_connection>> connections;
  std::unordered_map<solidarity::node_name, std::shared_ptr<mock_cluster_client>> clients;
  connections.reserve(cluster_size);
  clients.reserve(cluster_size);

  for (auto p : ports) {
    std::vector<unsigned short> out_ports;
    out_ports.reserve(ports.size() - 1);
    std::copy_if(ports.begin(),
                 ports.end(),
                 std::back_inserter(out_ports),
                 [p](const auto v) { return v != p; });

    EXPECT_EQ(out_ports.size(), ports.size() - 1);

    solidarity::mesh_connection::params_t params;
    params.listener_params.port = p;
    params.thread_count = 1;
    params.addrs.reserve(out_ports.size());
    std::transform(out_ports.begin(),
                   out_ports.end(),
                   std::back_inserter(params.addrs),
                   [](const auto prt) {
                     return solidarity::dialler::dial::params_t("localhost", prt);
                   });

    auto log_prefix = solidarity::utils::strings::args_to_string("localhost_", p, ": ");
    auto logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
        solidarity::utils::logging::logger_manager::instance()->get_shared_logger(),
        log_prefix);

    auto addr = solidarity::node_name().set_name(
        solidarity::utils::strings::args_to_string("node_", p));
    auto clnt = std::make_shared<mock_cluster_client>();
    auto c = std::make_shared<solidarity::mesh_connection>(addr, clnt, logger, params);
    connections.push_back(c);
    clients.insert({addr, clnt});
    c->start();
  }

  size_t without_one = cluster_size - 1;

  for (auto &v : connections) {
    auto target_clnt = dynamic_cast<mock_cluster_client *>(clients[v->self_addr()].get());
    while (true) {
      auto nds = v->all_nodes();
      tst_logger->info("wait node: ",
                       v->self_addr(),
                       " --> ",
                       nds.size(),
                       "    ",
                       target_clnt->connections_size());

      if (nds.size() == cluster_size && target_clnt->connections_size() == without_one) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
  }

  uint8_t cmd_index = 0;
  solidarity::append_entries ae;
  ae.cmd.data.resize(data_size);
  for (auto &v : connections) {
    auto other = v->all_nodes();
    for (auto &node : other) {
      if (node == v->self_addr()) {
        continue;
      }
      for (size_t i = 0; i < 3; ++i) {
        tst_logger->info(v->self_addr(), " => ", node);
        std::fill(ae.cmd.data.begin(), ae.cmd.data.end(), (unsigned char)i);
        i++;
        v->send_to(v->self_addr(), node, ae);
        auto target_clnt = dynamic_cast<mock_cluster_client *>(clients[node].get());
        while (true) {
          if (target_clnt->data_equal_to(ae.cmd.data)) {
            break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      }
    }
  }

  // send to all
  for (auto &v : connections) {
    auto other = v->all_nodes();
    tst_logger->info(v->self_addr(), " to all ");
    ae.cmd.data[0] = cmd_index;
    cmd_index++;
    v->send_all(v->self_addr(), ae);
    for (auto &node : other) {
      if (node == v->self_addr()) {
        continue;
      }
      auto target_clnt = dynamic_cast<mock_cluster_client *>(clients[node].get());
      while (true) {
        if (target_clnt->data.size() == ae.cmd.data.size()
            && target_clnt->data[0] == ae.cmd.data[0]) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }
  for (size_t i = 0; i < connections.size(); ++i) {
    auto v = connections[i];
    v->stop();
    for (size_t j = i + 1; j < connections.size(); ++j) {
      auto node = connections[j]->self_addr();
      auto c = dynamic_cast<mock_cluster_client *>(clients[node].get());
      while (!c->is_connection_lost(v->self_addr())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }
  connections.clear();
}