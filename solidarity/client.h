#pragma once

#include <solidarity/client_exception.h>
#include <solidarity/command.h>
#include <solidarity/error_codes.h>
#include <solidarity/event.h>
#include <solidarity/exports.h>
#include <solidarity/node_kind.h>
#include <solidarity/async_result.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>

#include <boost/asio.hpp>

namespace solidarity {
namespace dialler {
class dial;
}

class client;
class async_result_t;

namespace inner {
void client_update_connection_status(client &c, bool status);
void client_update_async_result(client &c,
                                uint64_t id,
                                const std::vector<uint8_t> &cmd,
                                solidarity::ERROR_CODE ec,
                                const std::string &err);

void client_notify_update(client &c, const client_event_t &ev);
} // namespace inner

struct send_result {
  ERROR_CODE ecode = solidarity::ERROR_CODE::UNDEFINED;
  command_status status = solidarity::command_status::APPLY_ERROR;

  bool is_ok() const {
    return ecode == ERROR_CODE::OK && status == command_status::WAS_APPLIED;
  }
};

class client {
public:
  struct params_t {
    params_t(const std::string &name_) { name = name_; }

    size_t threads_count = 1;
    std::string host;
    unsigned short port=0;
    std::string name;
  };
  client(const client &) = delete;
  client(client &&) = delete;
  client &operator=(const client &) = delete;

  EXPORT client(const params_t &p);
  EXPORT ~client();

  EXPORT void connect();
  EXPORT void disconnect();

  [[nodiscard]] EXPORT ERROR_CODE send_weak(const solidarity::command &cmd);
  [[nodiscard]] EXPORT send_result send_strong(const solidarity::command &cmd);
  EXPORT solidarity::command read(const solidarity::command &cmd);

  params_t params() const { return _params; }
  bool is_connected() const { return _connected; }

  EXPORT uint64_t add_event_handler(const std::function<void(const client_event_t &)> &);
  EXPORT void rm_event_handler(uint64_t);

  friend void inner::client_update_connection_status(client &c, bool status);
  friend void inner::client_update_async_result(client &c,
                                                uint64_t id,
                                                const std::vector<uint8_t> &cmd,
                                                solidarity::ERROR_CODE ec,
                                                const std::string &err);

  friend void inner::client_notify_update(client &c, const client_event_t &ev);

private:
  void notify_on_update(const client_event_t &);

private:
  params_t _params;
  std::atomic_bool _stoped = false;
  std::vector<std::thread> _threads;
  std::mutex _locker, _connect_locker;
  std::shared_ptr<dialler::dial> _dialler;
  std::atomic_size_t _threads_at_work;
  boost::asio::io_context _io_context;

  std::unordered_map<uint64_t, std::function<void(const client_event_t &)>>
      _on_update_handlers;
  async_result_handler _arh;

protected:
  bool _connected;
};
} // namespace solidarity
