#pragma once

#include <boost/asio.hpp>
#include <atomic>
#include <mutex>
#include <optional>
#include <solidarity/error_codes.h>
#include <solidarity/exports.h>
#include <solidarity/node_kind.h>
#include <string>
#include <thread>

namespace solidarity {
namespace dialler {
class dial;
}

class client;
class async_result_t;

class exception : public std::exception {
public:
  exception(std::string m)
      : _message(m) {}
  const char *what() const noexcept override { return _message.c_str(); }

private:
  std::string _message;
};

struct state_machine_updated_event_t {};

struct network_state_event_t {
  ERROR_CODE ecode = ERROR_CODE::OK;
};

struct raft_state_event_t {
  NODE_KIND old_state;
  NODE_KIND new_state;
};

struct client_event_t {
  enum class event_kind { RAFT, NETWORK, STATE_MACHINE };

  event_kind kind;
  std::optional<raft_state_event_t> raft_ev;
  std::optional<network_state_event_t> net_ev;
  std::optional<state_machine_updated_event_t> state_ev;
};

EXPORT std::string to_string(const client_event_t &cev);

namespace inner {
void client_update_connection_status(client &c, bool status);
void client_update_async_result(client &c,
                                uint64_t id,
                                const std::vector<uint8_t> &cmd,
                                solidarity::ERROR_CODE ec,
                                const std::string &err);

void client_notify_update(client &c, const client_event_t &ev);
} // namespace inner

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

  [[nodiscard]] EXPORT ERROR_CODE send(const std::vector<uint8_t> &cmd);
  EXPORT std::vector<uint8_t> read(const std::vector<uint8_t> &cmd);

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
  std::shared_ptr<async_result_t> make_waiter();
  void notify_on_update(const client_event_t &);

private:
  params_t _params;
  bool _stoped = false;
  std::vector<std::thread> _threads;
  std::shared_mutex _locker;
  std::shared_ptr<dialler::dial> _dialler;
  std::atomic_size_t _threads_at_work;
  boost::asio::io_context _io_context;

  std::atomic_uint64_t _next_query_id;
  std::unordered_map<uint64_t, std::shared_ptr<async_result_t>> _async_results;

  std::unordered_map<uint64_t, std::function<void(const client_event_t &)>>
      _on_update_handlers;

protected:
  bool _connected;
};
} // namespace solidarity
