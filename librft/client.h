#pragma once

#include <librft/exports.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace boost::asio {
class io_context;
} // namespace boost::asio

namespace dialler {
class dial;
}

namespace rft {

class client;
class async_result_t;

namespace inner {
void client_update_connection_status(client &c, bool status);
void client_update_async_result(client &c,
                                uint64_t id,
                                const std::vector<uint8_t> &cmd,
                                const std::string &err);
void client_notify_update(client &c);
} // namespace inner

class exception : public std::exception {
public:
  exception(std::string &m)
      : _message(m) {}
  const char *what() const noexcept override { return _message.c_str(); }

private:
  std::string _message;
};

class client {
public:
  struct params_t {
    params_t(const std::string &name_) { name = name_; }

    size_t threads_count = 1;
    std::string host;
    unsigned short port;
    std::string name;
  };
  client(const client &) = delete;
  client(client &&) = delete;
  client &operator=(const client &) = delete;

  EXPORT client(const params_t &p);
  EXPORT ~client();

  EXPORT void connect();
  EXPORT void disconnect();

  EXPORT void send(const std::vector<uint8_t> &cmd);
  EXPORT std::vector<uint8_t> read(const std::vector<uint8_t> &cmd);

  params_t params() const { return _params; }
  bool is_connected() const { return _connected; }
  
  uint64_t add_update_handler(const std::function<void()>&);
  void rm_update_handler(uint64_t);

  friend void inner::client_update_connection_status(client &c, bool status);
  friend void inner::client_update_async_result(client &c,
                                                uint64_t id,
                                                const std::vector<uint8_t> &cmd,
                                                const std::string &err);
  friend void inner::client_notify_update(client &c);

private:
  std::shared_ptr<async_result_t> make_waiter();
  void notify_on_update();

private:
  params_t _params;
  bool _stoped = false;
  std::vector<std::thread> _threads;
  std::mutex _locker;
  std::shared_ptr<dialler::dial> _dialler;
  std::atomic_size_t _threads_at_work;
  boost::asio::io_context _io_context;

  std::atomic_uint64_t _next_query_id;
  std::unordered_map<uint64_t, std::shared_ptr<async_result_t>> _async_results;

  std::unordered_map<uint64_t, std::function<void()>> _on_update_handlers;

protected:
  bool _connected;
};
} // namespace rft