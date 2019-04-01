#include <librft/abstract_cluster.h>
#include <librft/queries.h>
#include <catch.hpp>

#ifdef ENABLE_BENCHMARKS

TEST_CASE("serialisation", "[bench]") {
  rft::append_entries ae;

  ae.cmd.data.resize(1000);
  std::iota(ae.cmd.data.begin(), ae.cmd.data.end(), uint8_t(0));

  BENCHMARK("append_entries::to_byte_array") { ae.to_byte_array(); }

  auto ba = ae.to_byte_array();
  BENCHMARK("append_entries:from_byte_array") {
    auto e = rft::append_entries::from_byte_array(ba);
  }

  rft::queries::query_connect_t qcon(777, "long node id");
  BENCHMARK("query_connect_t::to_message") { qcon.to_message(); }

  auto qcon_msg = qcon.to_message();
  BENCHMARK("query_connect_t:unpack") {
    rft::queries::query_connect_t unpacked(qcon_msg);
  }

  rft::queries::connection_error_t con_error(777, "long error message");
  BENCHMARK("connection_error_t::to_message") { qcon.to_message(); }

  auto qcon_err_msg = con_error.to_message();
  BENCHMARK("connection_error_t:unpack") {
    rft::queries::connection_error_t unpacked(qcon_err_msg);
  }

  rft::queries::command_t cmd(rft::cluster_node("long cluster node name"), ae);
  BENCHMARK("command_t::to_message") { cmd.to_message(); }

  auto cmd_msg = cmd.to_message();
  BENCHMARK("command_t:unpack") { rft::queries::command_t unpacked(cmd_msg); }
}

#endif