#pragma once

#include <boost/asio.hpp>
#include <solidarity/abstract_cluster.h>
#include <solidarity/async_result.h>
#include <solidarity/config.h>
#include <solidarity/dialler/dialler.h>
#include <solidarity/dialler/listener.h>
#include <solidarity/dialler/message.h>
#include <solidarity/event.h>
#include <solidarity/protocol_version.h>
#include <solidarity/queries.h>
#include <solidarity/raft.h>
#include <solidarity/utils/logger.h>

namespace solidarity {
class mesh_connection;

namespace impl {

class out_connection final : public dialler::abstract_dial {
public:
  out_connection(const std::shared_ptr<mesh_connection> parent,
                 const std::string &target_addr);
  ~out_connection() override { _parent = nullptr; }
  void on_connect() override;
  void on_new_message(std::vector<dialler::message_ptr> &d, bool &cancel) override;
  void on_network_error(const boost::system::error_code &err) override;

private:
  std::shared_ptr<mesh_connection> _parent;
  std::string _target_addr;
};

class listener final : public dialler::abstract_listener_consumer {
public:
  listener(const std::shared_ptr<mesh_connection> parent);
  ~listener() override { _parent = nullptr; }
  void on_network_error(dialler::listener_client_ptr i,
                        const boost::system::error_code &err) override;

  void on_new_message(dialler::listener_client_ptr i,
                      std::vector<dialler::message_ptr> &d,
                      bool &cancel) override;

  bool on_new_connection(dialler::listener_client_ptr i) override;

  void on_disconnect(const dialler::listener_client_ptr &i) override;

private:
  std::shared_ptr<mesh_connection> _parent;
};

} // namespace impl

class mesh_connection final : public abstract_cluster,
                              public std::enable_shared_from_this<mesh_connection> {
public:
  struct params_t {
    params_t()
        : listener_params() {
      thread_count = std::thread::hardware_concurrency();
    }
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
  EXPORT ~mesh_connection() override;

  EXPORT void
  send_to(const node_name &from, const node_name &to, const append_entries &m) override;

  EXPORT void send_all(const node_name &from, const append_entries &m) override;
  EXPORT void send_all(const command_status_event_t &smuv);
  EXPORT void send_to(const node_name &to, const std::vector<dialler::message_ptr> &m);
  EXPORT size_t size() override;
  EXPORT std::vector<node_name> all_nodes() const override;

  friend impl::out_connection;
  friend impl::listener;

  node_name self_addr() const { return _self_addr; }

  boost::asio::io_context *context() { return &_io_context; }

  EXPORT std::shared_ptr<async_result_t>
  send_to(const solidarity::node_name &target,
          queries::resend_query_kind kind,
          const solidarity::command &cmd,
          std::function<void(ERROR_CODE)> callback);

  void set_state_machine_event_handler(
      const std::function<void(const command_status_event_t &)> h) {
    _on_smue_handler = h;
  }

  void stop_event_loop();

protected:
  void accept_out_connection(const node_name &name, const std::string &addr);
  void accept_input_connection(const node_name &name, uint64_t id);
  [[nodiscard]] node_name addr_by_id(uint64_t id);
  void rm_out_connection(const std::string &addr, const boost::system::error_code &err);
  void rm_input_connection(uint64_t id, const boost::system::error_code &err);
  void on_new_command(const std::vector<dialler::message_ptr> &m);

  void on_query_resend(const node_name &target,
                       uint64_t mess_id,
                       queries::resend_query_kind kind,
                       solidarity::command &cmd);
  void
  on_write_status(solidarity::node_name &target, uint64_t mess_id, ERROR_CODE status);
  void on_write_status(solidarity::node_name &target, ERROR_CODE status);

private:
  utils::logging::abstract_logger_ptr _logger;
  node_name _self_addr;

  mutable std::shared_mutex _locker;
  std::atomic_bool _stoped, _evl_stoped;
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

  // TODO dedicated type
  using message_id_to_callback
      = std::unordered_map<uint64_t, std::function<void(ERROR_CODE)>>;
  std::unordered_map<solidarity::node_name, message_id_to_callback> _messages;

  std::function<void(const command_status_event_t &)> _on_smue_handler;

  async_result_handler _ash;
};
} // namespace solidarity
