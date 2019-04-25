#include <solidarity/command.h>

#include "helpers.h"
#include <catch.hpp>

TEST_CASE("command.{from,to}_pod") {
  auto v = uint64_t(777);
  auto cmd = solidarity::command::from_value(v);
  EXPECT_EQ(cmd.to_value<uint64_t>(), v);
}

TEST_CASE("command.crc") {
  auto v = uint64_t(777);
  auto cmd = solidarity::command::from_value(v);
  auto crc1 = cmd.crc();
  cmd.data[0]++;
  auto crc2 = cmd.crc();
  EXPECT_NE(crc1, crc2);
}
