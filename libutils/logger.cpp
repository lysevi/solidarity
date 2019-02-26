#include <libutils/logger.h>
#include <libutils/utils.h>
#include <iostream>

using namespace utils::logging;
using namespace utils::async;

std::shared_ptr<logger_manager> logger_manager::_instance = nullptr;
utils::async::locker logger_manager::_locker;

verbose logger_manager::verbose = verbose::debug;

void logger_manager::start(abstract_logger_ptr &logger) {
  if (_instance == nullptr) {
    _instance = std::shared_ptr<logger_manager>{new logger_manager(logger)};
  }
}

void logger_manager::stop() {
  _instance = nullptr;
}

logger_manager *logger_manager::instance() noexcept {
  auto tmp = _instance.get();
  if (tmp == nullptr) {
    std::lock_guard<locker> lock(_locker);
    tmp = _instance.get();
    if (tmp == nullptr) {
      abstract_logger_ptr l = std::make_shared<console_logger>();
      _instance = std::make_shared<logger_manager>(l);
      tmp = _instance.get();
    }
  }
  return tmp;
}

logger_manager::logger_manager(abstract_logger_ptr &logger) {
  _logger = logger;
}


void console_logger::message(message_kind kind, const std::string &msg) noexcept {
  if (logger_manager::verbose == verbose::quiet) {
    return;
  }
  switch (kind) {
  case message_kind::fatal:
    std::cerr << "[err] " << msg << std::endl;
    break;
  case message_kind::warn:
    std::cout << "[wrn] " << msg << std::endl;
    break;
  case message_kind::info:
    std::cout << "[inf] " << msg << std::endl;
    break;
  case message_kind::message:
    if (logger_manager::verbose == verbose::debug) {
      std::cout << "[dbg] " << msg << std::endl;
    }
    break;
  }
}

void quiet_logger::message(message_kind kind, const std::string &msg) noexcept {
  UNUSED(kind);
  UNUSED(msg);
}