#pragma once

#include <librft/abstract_consumer.h>
#include <librft/command.h>
#include <librft/exports.h>
#include <librft/state.h>
#include <libutils/logger.h>

#include <string>
#include <thread>
#include <vector>

namespace dialler {
class listener;
class abstract_listener_consumer;

} // namespace dialler

namespace rft {
class mesh_connection;
class consensus;
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
              abstract_consensus_consumer *consumer);
  EXPORT ~node();

  params_t params() const { return _params; }
  EXPORT abstract_consensus_consumer *consumer();
  EXPORT std::shared_ptr<consensus> get_consensus();

  EXPORT void start();
  EXPORT void stop();
  EXPORT node_state_t state() const;
  EXPORT cluster_node self_name() const;

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
  std::shared_ptr<consensus> _consensus;
  std::shared_ptr<mesh_connection> _cluster_con;
  abstract_consensus_consumer *_consumer;

  utils::logging::abstract_logger_ptr _logger;

  std::shared_ptr<dialler::listener> _listener;
  std::shared_ptr<dialler::abstract_listener_consumer> _listener_consumer;

  mutable std::shared_mutex _locker;
  std::unordered_set<uint64_t> _clients;

  uint32_t _timer_period;
  std::unique_ptr<boost::asio::deadline_timer> _timer;
  std::unordered_map<uint64_t, std::vector<std::pair<uint64_t, rft::command>>>
      _message_resend;
};
} // namespace rft