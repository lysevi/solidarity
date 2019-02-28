#include <libutils/strings.h>
#include <benchmark/benchmark.h>

using namespace utils::strings;

static void BM_ArgsToString(benchmark::State &state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(args_to_string("Hello, world!", int(1), float(3.14),
                                            "Hello, Worl! Hello, world! Hello, Worl! "
                                            "Hello, world! Hello, Worl! Hello, world!",
                                            int(1), float(3.14),
                                            "Hello, Worl!"
                                            "Hello, world!",
                                            int(1), float(3.14), "Hello, Worl!"));
  }
}
BENCHMARK(BM_ArgsToString);

static void BM_SplitString(benchmark::State &state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(split("Hello, world!"
                                   "Hello, Worl! Hello, world! Hello, Worl! "
                                   "Hello, world! Hello, Worl! Hello, world!"
                                   "Hello, Worl!"
                                   "Hello, world!", ' '));
  }
}
BENCHMARK(BM_SplitString);