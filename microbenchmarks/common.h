#pragma once
#include <libutils/logger.h>

namespace microbenchmark_common {
class BenchmarkLogger : public utils::logging::abstract_logger {
public:
  BenchmarkLogger() {}
  ~BenchmarkLogger() {}
  void message(utils::logging::message_kind, const std::string &) noexcept {}
};

void replace_std_logger();
} // namespace microbenchmark_common