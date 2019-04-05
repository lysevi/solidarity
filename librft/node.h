#pragma once

#include <librft/abstract_consumer.h>
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
class cluster_connection;
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
  EXPORT node(const params_t &p, abstract_consensus_consumer *consumer);
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

private:
  params_t _params;
  std::shared_ptr<consensus> _consensus;
  std::shared_ptr<cluster_connection> _cluster_con;
  abstract_consensus_consumer *_consumer;

  utils::logging::abstract_logger_ptr _logger;

  std::shared_ptr<dialler::listener> _listener;
  std::shared_ptr<dialler::abstract_listener_consumer> _listener_consumer;

  mutable std::shared_mutex _locker;
  std::unordered_set<uint64_t> _clients;
};
} // namespace rft