#pragma once

#include <libutils/async/locker.h>
#include <libutils/strings.h>
#include <libutils/utils_exports.h>
#include <memory>
#include <utility>

namespace utils {
namespace logging {

enum class message_kind { message, info, warn, fatal };

class abstract_logger {
public:
  virtual void message(message_kind kind, const std::string &msg) noexcept = 0;
  virtual ~abstract_logger() {}

  template <typename... T>
  void variadic_message(message_kind kind, T &&... args) noexcept {
    auto str_message = utils::strings::args_to_string(args...);
    this->message(kind, str_message);
  }
};

using abstract_logger_ptr = std::shared_ptr<abstract_logger>;

class console_logger : public abstract_logger {
public:
  EXPORT void message(message_kind kind, const std::string &msg) noexcept override;
};

class quiet_logger : public abstract_logger {
public:
  EXPORT void message(message_kind kind, const std::string &msg) noexcept override;
};

enum class verbose { verbose, debug, quiet };

class logger_manager {
public:
  EXPORT static verbose verbose;
  logger_manager(abstract_logger_ptr &logger);

  EXPORT static void start(abstract_logger_ptr &logger);
  EXPORT static void stop();
  EXPORT static logger_manager *instance() noexcept;

  template <typename... T>
  void variadic_message(message_kind kind, T &&... args) noexcept {
    std::lock_guard<utils::async::locker> lg(_msg_locker);
    _logger->variadic_message(kind, std::forward<T>(args)...);
  }

private:
  static std::shared_ptr<logger_manager> _instance;
  static utils::async::locker _locker;
  utils::async::locker _msg_locker;
  abstract_logger_ptr _logger;
};

template <typename... T>
void logger(T &&... args) noexcept {
  utils::logging::logger_manager::instance()->variadic_message(
      utils::logging::message_kind::message, args...);
}

template <typename... T>
void logger_info(T &&... args) noexcept {
  utils::logging::logger_manager::instance()->variadic_message(
      utils::logging::message_kind::info, args...);
}

template <typename... T>
void logger_warn(T &&... args) noexcept {
  utils::logging::logger_manager::instance()->variadic_message(
      utils::logging::message_kind::warn, args...);
}

template <typename... T>
void logger_fatal(T &&... args) noexcept {
  utils::logging::logger_manager::instance()->variadic_message(
      utils::logging::message_kind::fatal, args...);
}
} // namespace logging
} // namespace utils
