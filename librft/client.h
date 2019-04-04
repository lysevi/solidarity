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
namespace inner {
void client_update_connection_status(client &c, bool status);
}

class client {
public:
  struct params_t {
    size_t threads_count = 1;
    std::string host;
    unsigned short port;
  };
  client(const client &) = delete;
  client(client &&) = delete;
  client &operator=(const client &) = delete;
  EXPORT client(const params_t &p);
  EXPORT ~client();
  EXPORT void connect();
  EXPORT void disconnect();

  params_t params() const { return _params; }
  bool is_connected() const { return _connected; }

  friend void inner::client_update_connection_status(client &c, bool status);

private:
  params_t _params;
  bool _stoped = false;
  std::vector<std::thread> _threads;
  std::mutex _locker;
  std::shared_ptr<dialler::dial> _dialler;
  std::atomic_size_t _threads_at_work;
  boost::asio::io_context _io_context;

protected:
  bool _connected;
};
} // namespace rft