#include <librft/utils/logger.h>
#include <benchmark/benchmark.h>

// BENCHMARK_MAIN();
int main(int argc, char **argv) {
  auto _raw_ptr = new rft::utils::logging::quiet_logger();
  auto _logger = rft::utils::logging::abstract_logger_ptr{_raw_ptr};
  rft::utils::logging::logger_manager::start(_logger);

  ::benchmark::Initialize(&argc, argv);
  if (::benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;
  ::benchmark::RunSpecifiedBenchmarks();
}