#pragma once

#include <solidarity/dialler/async_io.h>
#include <solidarity/dialler/initialized_resource.h>
#include <solidarity/dialler/listener_client.h>
#include <solidarity/exports.h>

#include <atomic>
#include <list>
#include <mutex>
#include <unordered_map>

namespace solidarity::dialler {
class listener;

class abstract_listener_consumer : public initialized_resource {
public:
  EXPORT virtual ~abstract_listener_consumer();

  virtual void on_network_error(listener_client_ptr i,
                                const boost::system::error_code &err)
      = 0;
  virtual void
  on_new_message(listener_client_ptr i, std::vector<message_ptr> &d, bool &cancel)
      = 0;
  virtual bool on_new_connection(listener_client_ptr i) = 0;
  virtual void on_disconnect(const listener_client_ptr &i) = 0;

  EXPORT void set_listener(const std::shared_ptr<listener> &lstnr);
  [[nodiscard]] EXPORT bool is_listener_exists() const { return _lstnr != nullptr; }
  EXPORT void send_to(uint64_t id, message_ptr d);
  EXPORT void stop();

private:
  std::shared_ptr<listener> _lstnr;
};

using abstract_listener_consumer_ptr = abstract_listener_consumer *;

class listener final : public std::enable_shared_from_this<listener>,
                       public initialized_resource {
public:
  struct params_t {
    unsigned short port;
  };
  listener() = delete;
  listener(const listener &) = delete;

  EXPORT listener(boost::asio::io_context *service, params_t p);
  EXPORT virtual ~listener();
  EXPORT void start();
  EXPORT void stop();

  EXPORT void send_to(listener_client_ptr i, message_ptr d);
  EXPORT void send_to(uint64_t id, message_ptr d);

  [[nodiscard]] EXPORT boost::asio::io_context *context() const { return _context; }

  EXPORT void erase_client_description(const listener_client_ptr client);
  EXPORT void add_consumer(const abstract_listener_consumer_ptr &c);

  EXPORT void erase_consumer();

  friend listener_client;

protected:
  void on_network_error(listener_client_ptr i, const boost::system::error_code &err);
  void on_new_message(listener_client_ptr i, std::vector<message_ptr> &d, bool &cancel);

private:
  void start_async_accept();

  EXPORT void on_accept_handler(const boost::system::error_code &err);

protected:
  boost::asio::io_context *_context = nullptr;
  std::shared_ptr<boost::asio::ip::tcp::acceptor> _acc = nullptr;
  std::shared_ptr<async_io> _aio = nullptr;
  std::atomic_int _next_id;

  abstract_listener_consumer_ptr _consumer;

  std::mutex _locker_connections;
  std::list<listener_client_ptr> _connections;

  params_t _params;
};
} // namespace solidarity::dialler
