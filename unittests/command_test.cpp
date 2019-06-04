#include <solidarity/command.h>

#include "helpers.h"
#include <catch.hpp>

TEST_CASE("command.{from,to}_pod") {
  auto v = uint64_t(777);
  auto cmd = solidarity::command::from_value(v);
  EXPECT_EQ(cmd.to_value<uint64_t>(), v);
}

TEST_CASE("command.ctor") {
  auto cmd = solidarity::command(77, {'a', 'b'});
  EXPECT_EQ(cmd.asm_num, 77);
}