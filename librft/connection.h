#pragma once

#include <librft/abstract_cluster.h>
#include <librft/config.h>
#include <librft/consensus.h>
#include <libdialler/dialler.h>
#include <libdialler/listener.h>
#include <libdialler/message.h>
#include <libutils/logger.h>

#include <boost/asio.hpp>

namespace rft {
class cluster_connection;
const uint16_t protocol_version = 1;
namespace impl {

class out_connection : public dialler::abstract_dial {
public:
  out_connection(const std::shared_ptr<cluster_connection> parent,
                 const cluster_node &target_addr);
  void on_connect() override;
  void on_new_message(dialler::message_ptr &&d, bool &cancel) override;
  void on_network_error(const dialler::message_ptr &d,
                        const boost::system::error_code &err) override;

private:
  std::shared_ptr<cluster_connection> _parent;
  cluster_node _target_addr;
  cluster_node _self_logical_addr;
};

class listener : public dialler::abstract_listener_consumer {
public:
  listener(const std::shared_ptr<cluster_connection> parent);

  void on_network_error(dialler::listener_client_ptr i,
                        const dialler::message_ptr &d,
                        const boost::system::error_code &err) override;

  void on_new_message(dialler::listener_client_ptr i,
                      dialler::message_ptr &&d,
                      bool &cancel) override;

  bool on_new_connection(dialler::listener_client_ptr i) override;

  void on_disconnect(const dialler::listener_client_ptr &i) override;

private:
  std::shared_ptr<cluster_connection> _parent;
  cluster_node _self_logical_addr;

  std::vector<dialler::message_ptr> _recv_message_pool;
};

} // namespace impl

class cluster_connection : public abstract_cluster,
                           public std::enable_shared_from_this<cluster_connection> {
public:
  struct params_t {
    params_t() { thread_count = std::thread::hardware_concurrency(); }
    dialler::listener::params_t listener_params;
    std::vector<dialler::dial::params_t> addrs;
    size_t thread_count = 0;
  };
  EXPORT cluster_connection(cluster_node self_addr,
                            const std::shared_ptr<abstract_cluster_client> &client,
                            const utils::logging::abstract_logger_ptr &logger,
                            const params_t &params);
  EXPORT void start();
  EXPORT void stop();
  EXPORT ~cluster_connection();

  EXPORT void send_to(const cluster_node &from,
                      const cluster_node &to,
                      const append_entries &m) override;

  EXPORT void send_all(const cluster_node &from, const append_entries &m) override;
  EXPORT size_t size() override;
  EXPORT std::vector<cluster_node> all_nodes() const override;

  friend impl::out_connection;
  friend impl::listener;

  cluster_node self_addr() const { return _self_addr; };

protected:
  void accept_out_connection(const cluster_node &name, const cluster_node &addr);
  void accept_input_connection(const cluster_node &name, uint64_t id);
  void rm_out_connection(const cluster_node &name);
  void rm_input_connection(const cluster_node &name);
  void on_new_command(const std::vector<dialler::message_ptr> &m);
  void heartbeat_timer();

private:
  utils::logging::abstract_logger_ptr _logger;
  cluster_node _self_addr;

  mutable std::shared_mutex _locker;
  bool _stoped;
  params_t _params;
  std::vector<std::thread> _threads;
  boost::asio::io_context _io_context;

  std::shared_ptr<dialler::abstract_listener_consumer> _listener_consumer;
  std::shared_ptr<dialler::listener> _listener;

  std::unordered_map<cluster_node, std::shared_ptr<dialler::dial>> _diallers;

  std::unordered_map<cluster_node, cluster_node>
      _accepted_out_connections; //  loigcal_name->addr
  std::unordered_map<cluster_node, uint64_t>
      _accepted_input_connections; // loigcal_name->id

  std::unique_ptr<boost::asio::deadline_timer> _timer;
  std::shared_ptr<abstract_cluster_client> _client;
};
} // namespace rft