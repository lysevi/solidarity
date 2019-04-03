#pragma once

#include <librft/consensus.h>
#include <librft/exports.h>
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
  EXPORT node(const params_t &p, abstract_consensus_consumer *consumer);
  EXPORT ~node();
  params_t params() const { return _params; }
  
  EXPORT void start();
  EXPORT void stop();
  EXPORT node_state_t state() const;
  EXPORT cluster_node self_name() const;

private:
  params_t _params;
  std::shared_ptr<consensus> _consensus;
  std::shared_ptr<cluster_connection> _cluster_con;
  abstract_consensus_consumer *_consumer;

  utils::logging::abstract_logger_ptr _logger;

  std::shared_ptr<dialler::listener> _listener;
  std::shared_ptr<dialler::abstract_listener_consumer> _listener_consumer;
};
} // namespace rft