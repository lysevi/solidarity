#include <libsolidarity/utils/strings.h>
#include <catch.hpp>

#ifdef ENABLE_BENCHMARKS
TEST_CASE("utils::strings", "[bench]") {

  BENCHMARK("strings::args_to_string small") {
    utils::strings::args_to_string("Hello, world!", int(1), float(3.14));
  }

  BENCHMARK("strings::args_to_string") {
    utils::strings::args_to_string("Hello, world!", int(1), float(3.14),
                                   "Hello, World! Hello, world! Hello, World! "
                                   "Hello, world! Hello, Worl! Hello, world!",
                                   int(1), float(3.14),
                                   "Hello, World!"
                                   "Hello, world!",
                                   int(1), float(3.14), "Hello, World!");
  }

  std::string target("H e l l o , w o r l d !", 10);

  BENCHMARK("strings::split") { utils::strings::split(target, ' '); }
}
#endif