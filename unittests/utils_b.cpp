#include <catch.hpp>
#include <numeric>
#include <solidarity/utils/crc.h>
#include <solidarity/utils/strings.h>
#include <solidarity/utils/utils.h>

#ifdef ENABLE_BENCHMARKS
TEST_CASE("utils::strings", "[bench]") {
  const size_t data_size = 1024 * 1024;
  std::vector<uint8_t> data(data_size);
  std::iota(data.begin(), data.end(), uint8_t(0));
  solidarity::utils::crc32 crc_res;
  BENCHMARK("crc") { UNUSED(crc_res.calculate(data.cbegin(), data.cend())); };

  BENCHMARK("strings::to_string small") {
    UNUSED(solidarity::utils::strings::to_string("Hello, world!", int(1), float(3.14)));
  };

  BENCHMARK("strings::to_string") {
    UNUSED(
        solidarity::utils::strings::to_string("Hello, world!",
                                              int(1),
                                              float(3.14),
                                              "Hello, World! Hello, world! Hello, World! "
                                              "Hello, world! Hello, Worl! Hello, world!",
                                              int(1),
                                              float(3.14),
                                              "Hello, World!"
                                              "Hello, world!",
                                              int(1),
                                              float(3.14),
                                              "Hello, World!"));
  };

  std::string target("H e l l o , w o r l d !", 10);

  BENCHMARK("strings::split") { UNUSED(solidarity::utils::strings::split(target, ' ')); };
}
#endif