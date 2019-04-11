#pragma once

#include <librft/abstract_cluster.h>
#include <librft/config.h>
#include <librft/consensus.h>
#include <librft/protocol_version.h>
#include <libdialler/dialler.h>
#include <libdialler/listener.h>
#include <libdialler/message.h>
#include <libutils/logger.h>

#include <boost/asio.hpp>

namespace rft {
class mesh_connection;

namespace impl {

class out_connection : public dialler::abstract_dial {
public:
  out_connection(const std::shared_ptr<mesh_connection> parent,
                 const std::string &target_addr);
  void on_connect() override;
  void on_new_message(dialler::message_ptr &&d, bool &cancel) override;
  void on_network_error(const dialler::message_ptr &d,
                        const boost::system::error_code &err) override;

private:
  std::shared_ptr<mesh_connection> _parent;
  std::string _target_addr;
};

class listener : public dialler::abstract_listener_consumer {
public:
  listener(const std::shared_ptr<mesh_connection> parent);

  void on_network_error(dialler::listener_client_ptr i,
                        const dialler::message_ptr &d,
                        const boost::system::error_code &err) override;

  void on_new_message(dialler::listener_client_ptr i,
                      dialler::message_ptr &&d,
                      bool &cancel) override;

  bool on_new_connection(dialler::listener_client_ptr i) override;

  void on_disconnect(const dialler::listener_client_ptr &i) override;

private:
  std::shared_ptr<mesh_connection> _parent;
  // node_name _self_logical_addr;

  std::vector<dialler::message_ptr> _recv_message_pool;
};

} // namespace impl

class mesh_connection : public abstract_cluster,
                        public std::enable_shared_from_this<mesh_connection> {
public:
  struct params_t {
    params_t() { thread_count = std::thread::hardware_concurrency(); }
    dialler::listener::params_t listener_params;
    std::vector<dialler::dial::params_t> addrs;
    size_t thread_count = 0;
  };
  EXPORT mesh_connection(node_name self_addr,
                         const std::shared_ptr<abstract_cluster_client> &client,
                         const utils::logging::abstract_logger_ptr &logger,
                         const params_t &params);
  EXPORT void start();
  EXPORT void stop();
  EXPORT ~mesh_connection();

  EXPORT void
  send_to(const node_name &from, const node_name &to, const append_entries &m) override;

  EXPORT void send_all(const node_name &from, const append_entries &m) override;
  EXPORT size_t size() override;
  EXPORT std::vector<node_name> all_nodes() const override;

  friend impl::out_connection;
  friend impl::listener;

  node_name self_addr() const { return _self_addr; };

  boost::asio::io_context *context() { return &_io_context; }

  void send_to(rft::node_name &target,
               rft::command &cmd,
               std::function<void(ERROR_CODE)> callback);

protected:
  void accept_out_connection(const node_name &name, const std::string &addr);
  void accept_input_connection(const node_name &name, uint64_t id);
  node_name addr_by_id(uint64_t id);
  void rm_out_connection(const std::string &addr);
  void rm_input_connection(uint64_t id);
  void on_new_command(const std::vector<dialler::message_ptr> &m);

  void on_write_resend(const node_name &target, uint64_t mess_id, rft::command &cmd);
  void on_write_status(rft::node_name &target, uint64_t mess_id, ERROR_CODE status);

private:
  utils::logging::abstract_logger_ptr _logger;
  node_name _self_addr;

  mutable std::shared_mutex _locker;
  bool _stoped;
  params_t _params;

  std::vector<std::thread> _threads;
  std::atomic_size_t _threads_at_work;

  boost::asio::io_context _io_context;

  std::shared_ptr<dialler::abstract_listener_consumer> _listener_consumer;
  std::shared_ptr<dialler::listener> _listener;

  std::unordered_map<std::string, std::shared_ptr<dialler::dial>> _diallers;

  ///  logical_name->addr
  std::unordered_map<node_name, std::string> _accepted_out_connections;
  /// id -> loigcal_name
  std::unordered_map<uint64_t, node_name> _accepted_input_connections;

  std::shared_ptr<abstract_cluster_client> _client;

  std::atomic_size_t _message_id;
  // TODO dedicated type
  using message_id_to_callback
      = std::unordered_map<uint64_t, std::function<void(ERROR_CODE)>>;
  std::unordered_map<rft::node_name, message_id_to_callback> _messages;
};
} // namespace rft