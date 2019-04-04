#include <librft/abstract_cluster.h>
#include <librft/queries.h>
#include <libutils/utils.h>
#include <catch.hpp>

#ifdef ENABLE_BENCHMARKS

TEST_CASE("serialisation", "[bench]") {
  using namespace rft::queries;
  using dialler::message;
  rft::append_entries ae;

  ae.cmd.data.resize(1000);
  std::iota(ae.cmd.data.begin(), ae.cmd.data.end(), uint8_t(0));

  BENCHMARK("append_entries::to_byte_array") { UNUSED(ae.to_byte_array()); }

  auto ba = ae.to_byte_array();
  BENCHMARK("append_entries:from_byte_array") {
    auto e = rft::append_entries::from_byte_array(ba);
  }

  query_connect_t qcon(777, "long node id");
  BENCHMARK("query_connect_t::to_message") { UNUSED(qcon.to_message()); }

  auto qcon_msg = qcon.to_message();
  BENCHMARK("query_connect_t:unpack") { query_connect_t unpacked(qcon_msg); }

  connection_error_t con_error(777, "long error message");
  BENCHMARK("connection_error_t::to_message") { UNUSED(qcon.to_message()); }

  auto qcon_err_msg = con_error.to_message();
  BENCHMARK("connection_error_t:unpack") { connection_error_t unpacked(qcon_err_msg); }

  status_t status_(777, "long error message");
  BENCHMARK("status_t::to_message") { UNUSED(status_.to_message()); }

  auto status_msg = status_.to_message();
  BENCHMARK("status_t:unpack") { status_t unpacked(qcon_err_msg); }

  command_t cmd(rft::cluster_node("long cluster node name"), ae);
  BENCHMARK("command_t::to_message") { UNUSED(cmd.to_message()); }

  auto cmd_msg = cmd.to_message();
  BENCHMARK("command_t:unpack") { command_t unpacked(cmd_msg); }

  ae.cmd.data.resize(message::MAX_BUFFER_SIZE * 11);
  std::iota(ae.cmd.data.begin(), ae.cmd.data.end(), uint8_t(0));
  BENCHMARK("command_t::to_message(big)") { cmd.to_message(); }
  cmd_msg = cmd.to_message();
  BENCHMARK("command_t:unpack(big)") { command_t unpacked(cmd_msg); }

  clients::client_connect_t client_con(777);
  BENCHMARK("client_connect_t::to_message") { UNUSED(qcon.to_message()); }

  auto clcon_msg = client_con.to_message();
  BENCHMARK("client_connect_t:unpack") { clients::client_connect_t unpacked(clcon_msg); }

  clients::read_query_t read_q(777, ae.cmd);
  BENCHMARK("read_query_t::to_message") { UNUSED(read_q.to_message()); }

  auto read_q_msg = read_q.to_message();
  BENCHMARK("read_query_t:unpack") { clients::read_query_t unpacked(read_q_msg); }

  clients::write_query_t write_q(777, ae.cmd);
  BENCHMARK("write_query_t::to_message") { UNUSED(write_q.to_message()); }

  auto write_q_msg = write_q.to_message();
  BENCHMARK("write_query_t:unpack") { clients::write_query_t unpacked(write_q_msg); }
}

#endif