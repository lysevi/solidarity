#include "helpers.h"
#include <librft/node.h>
#include <catch.hpp>

TEST_CASE("node.single") {
  auto settings =
      rft::node_settings().set_name("_0").set_period(std::chrono::milliseconds(1000));
  rft::node m(settings);
}