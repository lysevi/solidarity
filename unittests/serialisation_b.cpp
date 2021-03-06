#include <catch.hpp>
#include <numeric>
#include <solidarity/append_entries.h>
#include <solidarity/queries.h>
#include <solidarity/utils/utils.h>

#ifdef ENABLE_BENCHMARKS

using namespace solidarity;

TEST_CASE("serialisation", "[bench]") {
  using namespace solidarity::queries;
  using dialler::message;
  {
    solidarity::append_entries ae;
    ae.cmd.resize(1000);
    std::iota(ae.cmd.data.begin(), ae.cmd.data.end(), uint8_t(0));

    BENCHMARK("append_entries::to_byte_array") { UNUSED(ae.to_byte_array()); };

    auto ba = ae.to_byte_array();
    BENCHMARK("append_entries:from_byte_array") {
      auto e = solidarity::append_entries::from_byte_array(ba);
    };
  }
  {
    query_connect_t qcon(777, "long node id");
    BENCHMARK("query_connect_t::to_message") { UNUSED(qcon.to_message()); };

    auto qcon_msg = qcon.to_message();
    BENCHMARK("query_connect_t:unpack") { query_connect_t unpacked(qcon_msg); };
  }
  {
    connection_error_t con_error(
        777, solidarity::ERROR_CODE::WRONG_PROTOCOL_VERSION, "long error message");
    BENCHMARK("connection_error_t::to_message") { UNUSED(con_error.to_message()); };

    auto qcon_err_msg = con_error.to_message();
    BENCHMARK("connection_error_t:unpack") { connection_error_t unpacked(qcon_err_msg); };
  }
  {
    status_t status_(777, solidarity::ERROR_CODE::OK, "long error message");
    BENCHMARK("status_t::to_message") { UNUSED(status_.to_message()); };

    auto status_msg = status_.to_message();
    BENCHMARK("status_t:unpack") { status_t unpacked(status_msg); };
  }

  {
    solidarity::append_entries ae;
    ae.cmd.resize(1000);
    std::iota(ae.cmd.begin(), ae.cmd.end(), uint8_t(0));

    add_command_t cmd(solidarity::node_name("long cluster node name"), ae);
    BENCHMARK("command_t::to_message") { UNUSED(cmd.to_message()); };

    auto cmd_msg = cmd.to_message();
    BENCHMARK("command_t:unpack") { add_command_t unpacked(cmd_msg); };
  }
  {
    clients::client_connect_t client_con("client name", 777);
    BENCHMARK("client_connect_t::to_message") { UNUSED(client_con.to_message()); };

    auto clcon_msg = client_con.to_message();
    BENCHMARK("client_connect_t:unpack") {
      clients::client_connect_t unpacked(clcon_msg);
    };
  }
  {
    solidarity::command_t read_q_cmd;
    read_q_cmd.resize(10);
    std::iota(read_q_cmd.begin(), read_q_cmd.end(), uint8_t(0));
    clients::read_query_t read_q(777, read_q_cmd);
    BENCHMARK("read_query_t::to_message") { UNUSED(read_q.to_message()); };

    auto read_q_msg = read_q.to_message();
    BENCHMARK("read_query_t:unpack") { clients::read_query_t unpacked(read_q_msg); };
  }
  {
    solidarity::command_t w_q_cmd;
    w_q_cmd.resize(10);
    std::iota(w_q_cmd.begin(), w_q_cmd.end(), uint8_t(0));
    clients::write_query_t write_q(777, w_q_cmd);
    BENCHMARK("write_query_t::to_message") { UNUSED(write_q.to_message()); };

    auto write_q_msg = write_q.to_message();
    BENCHMARK("write_query_t:unpack") { clients::write_query_t unpacked(write_q_msg); };
  }

  {
    solidarity::command_status_event_t smev;
    smev.crc = 33;
    smev.status = solidarity::command_status::CAN_BE_APPLY;
    clients::command_status_query_t state_machine_u(smev);
    BENCHMARK("state_machine_updated_t::to_message") {
      UNUSED(state_machine_u.to_message());
    };

    auto sm_q = state_machine_u.to_message();
    BENCHMARK("state_machine_updated_t:unpack") {
      clients::command_status_query_t unpacked(sm_q);
    };
  }
}

#endif