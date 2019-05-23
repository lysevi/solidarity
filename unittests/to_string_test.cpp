#include "helpers.h"
#include <catch.hpp>
#include <solidarity/event.h>

#include <solidarity/error_codes.h>

TEST_CASE("to_string.command_status") {
  for (uint8_t i = 0; i < (uint8_t)solidarity::command_status::LAST; ++i) {
    solidarity::command_status s = (solidarity::command_status)i;
    EXPECT_TRUE(solidarity::to_string(s).size() != 0);
  }
}

TEST_CASE("to_string.error_code") {
  for (uint8_t i = 0; i < (uint8_t)solidarity::ERROR_CODE::LAST; ++i) {
    solidarity::ERROR_CODE s = (solidarity::ERROR_CODE)i;
    EXPECT_TRUE(solidarity::to_string(s).size() != 0);
  }

  solidarity::ERROR_CODE s = solidarity::ERROR_CODE::UNDEFINED;
  EXPECT_TRUE(solidarity::to_string(s).size() != 0);
}

TEST_CASE("to_string.client_event_t") {
  for (uint8_t i = 0; i < (uint8_t)solidarity::client_event_t::event_kind::LAST; ++i) {
    auto s = (solidarity::client_event_t::event_kind)i;
    solidarity::client_event_t cev;
    cev.kind = s;

    solidarity::raft_state_event_t rse;
    rse.new_state = solidarity::NODE_KIND::CANDIDATE;
    rse.old_state = solidarity::NODE_KIND::CANDIDATE;
    cev.raft_ev = rse;

    solidarity::command_status_event_t cse;
    cse.crc = 11;
    cse.status = solidarity::command_status::WAS_APPLIED;
    cev.cmd_ev = cse;

    solidarity::network_state_event_t nse;
    cev.net_ev = nse;

    EXPECT_TRUE(solidarity::to_string(cev).size() != 0);
  }
}
