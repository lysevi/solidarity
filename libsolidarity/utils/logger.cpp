#include <fstream>
#include <iostream>
#include <libsolidarity/utils/logger.h>
#include <libsolidarity/utils/utils.h>

using namespace solidarity::utils::logging;
using namespace solidarity::utils::async;

std::shared_ptr<logger_manager> logger_manager::_instance = nullptr;
solidarity::utils::async::locker logger_manager::_locker;

VERBOSE_KIND logger_manager::verbose = VERBOSE_KIND::debug;

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

abstract_logger *logger_manager::get_logger() noexcept {
  return _logger.get();
}

abstract_logger_ptr logger_manager::get_shared_logger() noexcept {
  return _logger;
}

logger_manager::logger_manager(abstract_logger_ptr &logger) {
  _logger = logger;
}

void console_logger::message(MESSAGE_KIND kind, const std::string &msg) noexcept {
  if (logger_manager::verbose == VERBOSE_KIND::quiet) {
    return;
  }
  switch (kind) {
  case MESSAGE_KIND::fatal:
    std::cerr << "[err] " << msg << std::endl;
    break;
  case MESSAGE_KIND::warn:
    std::cout << "[wrn] " << msg << std::endl;
    break;
  case MESSAGE_KIND::info:
    std::cout << "[inf] " << msg << std::endl;
    break;
  case MESSAGE_KIND::message:
    if (logger_manager::verbose == VERBOSE_KIND::debug) {
      std::cout << "[dbg] " << msg << std::endl;
    }
    break;
  }
}

void quiet_logger::message(MESSAGE_KIND kind, const std::string &msg) noexcept {
  UNUSED(kind);
  UNUSED(msg);
}

file_logger::file_logger(std::string fname, bool verbose) {
  _verbose = verbose;

  std::stringstream fname_ss;
  fname_ss << fname;
  fname_ss << ".log";

  auto logname = fname_ss.str();
  if (verbose) {
    std::cout << "See output in " << logname << std::endl;
  }
  _output = std::make_unique<std::ofstream>(logname, std::ofstream::app);
  (*_output) << "Start programm" << std::endl;
}

void file_logger::message(MESSAGE_KIND kind, const std::string &msg) noexcept {
  std::lock_guard lg(_locker);

  std::stringstream ss;
  switch (kind) {
  case solidarity::utils::logging::MESSAGE_KIND::fatal:
    ss << "[err] " << msg << std::endl;
    break;
  case solidarity::utils::logging::MESSAGE_KIND::info:
    ss << "[inf] " << msg << std::endl;
    break;
  case solidarity::utils::logging::MESSAGE_KIND::warn:
    ss << "[wrn] " << msg << std::endl;
    break;
  case solidarity::utils::logging::MESSAGE_KIND::message:
    ss << "[dbg] " << msg << std::endl;
    break;
  }

  (*_output) << ss.str();
  _output->flush();
  _output->flush();

  if (_verbose || kind == solidarity::utils::logging::MESSAGE_KIND::fatal) {
    std::cerr << ss.str();
  }
}