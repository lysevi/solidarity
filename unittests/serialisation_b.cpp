#include <librft/abstract_cluster.h>
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
}
#endif