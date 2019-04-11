#pragma once

#include <libsolidarity/abstract_state_machine.h>
#include <libsolidarity/command.h>
#include <libsolidarity/exports.h>
#include <libsolidarity/raft_state.h>
#include <libutils/logger.h>

#include <string>
#include <thread>
#include <vector>

namespace dialler {
class listener;
class abstract_listener_consumer;

} // namespace dialler

namespace solidarity {
class mesh_connection;
class raft;
class state;
class node {
public:
  struct params_t {
    params_t() { thread_count = std::thread::hardware_concurrency(); }
    size_t thread_count = 0;
    unsigned short port;
    unsigned short client_port;
    std::vector<std::string> cluster;
    std::string name;
  };
  node() = delete;
  node(const node &) = delete;
  node(const node &&) = delete;
  node &operator=(const node &) = delete;
  EXPORT node(utils::logging::abstract_logger_ptr logger,
              const params_t &p,
              abstract_state_machine *state_machine);
  EXPORT ~node();

  params_t params() const { return _params; }
  EXPORT abstract_state_machine *state_machine();
  EXPORT std::shared_ptr<raft> get_raft();

  EXPORT void start();
  EXPORT void stop();
  EXPORT raft_state_t state() const;
  EXPORT node_name self_name() const;

  void add_client(uint64_t id);
  void rm_client(uint64_t id);
  EXPORT size_t connections_count() const;

  void notify_state_machine_update();
  void send_to_leader(uint64_t client_id, uint64_t message_id, command &cmd);

private:
  void heartbeat_timer();
  void on_message_sended_status(uint64_t client, uint64_t message, ERROR_CODE status);

private:
  bool _stoped;
  params_t _params;
  std::shared_ptr<raft> _raft;
  std::shared_ptr<mesh_connection> _cluster_con;
  abstract_state_machine *_state_machine;

  utils::logging::abstract_logger_ptr _logger;

  std::shared_ptr<dialler::listener> _listener;
  std::shared_ptr<dialler::abstract_listener_consumer> _listener_consumer;

  mutable std::shared_mutex _locker;
  std::unordered_set<uint64_t> _clients;

  uint32_t _timer_period;
  std::unique_ptr<boost::asio::deadline_timer> _timer;
  std::unordered_map<uint64_t, std::vector<std::pair<uint64_t, solidarity::command>>>
      _message_resend;
};
} // namespace solidarity