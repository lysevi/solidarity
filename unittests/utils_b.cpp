#include <catch.hpp>
#include <solidarity/utils/strings.h>
#include <solidarity/utils/utils.h>

#ifdef ENABLE_BENCHMARKS
TEST_CASE("utils::strings", "[bench]") {

  BENCHMARK("strings::args_to_string small") {
    UNUSED(
        solidarity::utils::strings::args_to_string("Hello, world!", int(1), float(3.14)));
  }

  BENCHMARK("strings::args_to_string") {
    UNUSED(solidarity::utils::strings::args_to_string(
        "Hello, world!",
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
  }

  std::string target("H e l l o , w o r l d !", 10);

  BENCHMARK("strings::split") { UNUSED(solidarity::utils::strings::split(target, ' ')); }
}
#endif