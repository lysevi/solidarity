#pragma once
#include <librft/utils/logger.h>
namespace microbenchmark_common {
class BenchmarkLogger : public rft::utils::logging::abstract_logger {
public:
  BenchmarkLogger() {}
  ~BenchmarkLogger() {}
  void message(rft::utils::logging::message_kind, const std::string &) noexcept {}
};

void replace_std_logger();
} // namespace microbenchmark_common