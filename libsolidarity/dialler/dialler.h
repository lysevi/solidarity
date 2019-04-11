#pragma once

#include <libsolidarity/dialler/async_io.h>
#include <libsolidarity/exports.h>
#include <libsolidarity/dialler/initialized_resource.h>
#include <libsolidarity/dialler/message.h>
#include <unordered_map>

namespace solidarity::dialler {

class dial;
class abstract_dial {
public:
  EXPORT virtual ~abstract_dial();
  virtual void on_connect() = 0;
  virtual void on_new_message(message_ptr &&d, bool &cancel) = 0;
  virtual void on_network_error(const message_ptr &d,
                                const boost::system::error_code &err)
      = 0;

  EXPORT bool is_connected() const;
  EXPORT bool is_stoped() const;

  EXPORT void add_connection(std::shared_ptr<dial> c);
  EXPORT bool is_connection_exists() const { return _connection != nullptr; }

protected:
  std::shared_ptr<dial> _connection;
};
using abstract_connection_consumer_ptr = std::shared_ptr<abstract_dial>;

class dial final : public std::enable_shared_from_this<dial>,
                   public initialized_resource {
public:
  struct params_t {
    params_t(std::string host_, unsigned short port_, bool auto_reconnection_ = true)
        : host(host_)
        , port(port_)
        , auto_reconnection(auto_reconnection_) {}
    std::string host;
    unsigned short port;
    bool auto_reconnection = true;

    bool operator==(const params_t &other) const {
      return host == other.host && port == other.port
          && auto_reconnection == other.auto_reconnection;
    }
  };
  dial() = delete;
  params_t get_params() const { return _params; }

  EXPORT dial(boost::asio::io_context *context, const params_t &_parms);
  EXPORT virtual ~dial();
  EXPORT void disconnect();
  EXPORT void start_async_connection();
  EXPORT void reconnecton_error(const message_ptr &d,
                                const boost::system::error_code &err);
  EXPORT void on_data_receive(message_ptr &&d, bool &cancel);
  EXPORT void send_async(const message_ptr &d);
  EXPORT void send_async(const std::vector<message_ptr> &d);

  EXPORT void add_consumer(const abstract_connection_consumer_ptr &c);
  EXPORT void erase_consumer();

protected:
  std::shared_ptr<async_io> _async_io = nullptr;
  boost::asio::io_context *_context = nullptr;
  params_t _params;

  abstract_connection_consumer_ptr _consumers;
};

} // namespace dialler

namespace std {
template <>
class hash<solidarity::dialler::dial::params_t> {
public:
  size_t operator()(const solidarity::dialler::dial::params_t &s) const {
    size_t h = std::hash<std::string>()(s.host);
    size_t h2 = std::hash<unsigned short>()(s.port);
    size_t h3 = std::hash<bool>()(s.auto_reconnection);
    return h ^ h2 ^ h3;
  }
};
} // namespace std