#pragma once

#include <solidarity/exports.h>
#include <solidarity/utils/strings.h>

#include <fstream>
#include <memory>
#include <mutex>
#include <utility>

namespace solidarity::utils::logging {

enum class MESSAGE_KIND { message, info, warn, fatal };

class abstract_logger {
public:
  virtual void message(MESSAGE_KIND kind, const std::string &msg) noexcept = 0;
  virtual ~abstract_logger() {}

  template <typename... T>
  void variadic_message(MESSAGE_KIND kind, T &&... args) noexcept {
    auto str_message = utils::strings::to_string(args...);
    this->message(kind, str_message);
  }

  template <typename... T>
  void dbg(T &&... args) noexcept {
    variadic_message(utils::logging::MESSAGE_KIND::message, args...);
  }

  template <typename... T>
  void info(T &&... args) noexcept {
    variadic_message(utils::logging::MESSAGE_KIND::info, args...);
  }

  template <typename... T>
  void warn(T &&... args) noexcept {
    variadic_message(utils::logging::MESSAGE_KIND::warn, args...);
  }

  template <typename... T>
  void fatal(T &&... args) noexcept {
    variadic_message(utils::logging::MESSAGE_KIND::fatal, args...);
  }
};

using abstract_logger_ptr = std::shared_ptr<abstract_logger>;
using abstract_logger_uptr = std::unique_ptr<abstract_logger>;

class prefix_logger : public abstract_logger {
public:
  prefix_logger(abstract_logger_ptr target, const std::string &prefix)
      : _prefix(prefix)
      , _shared_target(target) {}
  ~prefix_logger() override { _shared_target = nullptr; }
  void message(MESSAGE_KIND kind, const std::string &msg) noexcept override {
    _shared_target->message(kind, utils::strings::to_string(_prefix, msg));
  }

private:
  const std::string _prefix;
  abstract_logger_ptr _shared_target;
};

class console_logger final : public abstract_logger {
public:
  EXPORT void message(MESSAGE_KIND kind, const std::string &msg) noexcept override;
};

class quiet_logger final : public abstract_logger {
public:
  EXPORT void message(MESSAGE_KIND kind, const std::string &msg) noexcept override;
};

class file_logger final : public abstract_logger {
public:
  EXPORT file_logger(std::string fname, bool _verbose = false);
  EXPORT void message(MESSAGE_KIND kind, const std::string &msg) noexcept override;

private:
  bool _verbose;
  std::unique_ptr<std::ofstream> _output;
  std::mutex _locker;
};

enum class VERBOSE_KIND { verbose, debug, quiet };

class logger_manager {
public:
  EXPORT static VERBOSE_KIND verbose;
  logger_manager(abstract_logger_ptr &logger);
  [[nodiscard]] EXPORT abstract_logger *get_logger() noexcept;
  [[nodiscard]] EXPORT abstract_logger_ptr get_shared_logger() noexcept;

  EXPORT static void start(abstract_logger_ptr &logger);
  EXPORT static void stop();
  [[nodiscard]] EXPORT static logger_manager *instance() noexcept;

  template <typename... T>
  void variadic_message(MESSAGE_KIND kind, T &&... args) noexcept {
    std::lock_guard lg(_msg_locker);
    _logger->variadic_message(kind, std::forward<T>(args)...);
  }

private:
  static std::shared_ptr<logger_manager> _instance;
  static std::mutex _locker;
  std::mutex _msg_locker;
  abstract_logger_ptr _logger;
};

template <typename... T>
void logger(T &&... args) noexcept {
  utils::logging::logger_manager::instance()->get_logger()->dbg(args...);
}

template <typename... T>
void logger_info(T &&... args) noexcept {
  utils::logging::logger_manager::instance()->get_logger()->info(args...);
}

template <typename... T>
void logger_warn(T &&... args) noexcept {
  utils::logging::logger_manager::instance()->get_logger()->warn(args...);
}

template <typename... T>
void logger_fatal(T &&... args) noexcept {
  utils::logging::logger_manager::instance()->get_logger()->fatal(args...);
}
} // namespace solidarity::utils::logging
