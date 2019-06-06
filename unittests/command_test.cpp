#include <solidarity/command.h>

#include "helpers.h"
#include <algorithm>

#include <catch.hpp>

TEST_CASE("command.{from,to}_pod") {
  auto v = uint64_t(777);
  auto cmd = solidarity::command_t::from_value(v);
  EXPECT_EQ(cmd.to_value<uint64_t>(), v);
}

TEST_CASE("command.ctor") {
  auto cmd = solidarity::command_t(77, {'a', 'b'});
  EXPECT_EQ(cmd.asm_num, 77);
}

TEST_CASE("command.serialisation") {
  auto cmd = solidarity::command_t(77, {'a', 'b'});
  auto ba = cmd.to_byte_array();
  auto cmd2 = solidarity::command_t::from_byte_array(ba);
  EXPECT_EQ(cmd2.asm_num, cmd.asm_num);
  EXPECT_TRUE(std::equal(cmd.data.begin(), cmd.data.end(), cmd2.data.begin()));
}