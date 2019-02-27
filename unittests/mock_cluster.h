#pragma once

#include <librft/abstract_cluster.h>
#include <librft/consensus.h>
#include <condition_variable>
#include <map>
#include <shared_mutex>
#include <tuple>

class mock_cluster : public rft::abstract_cluster {
  struct message_t {
    rft::cluster_node from;
    rft::cluster_node to;
    rft::append_entries m;
  };

public:
  mock_cluster();

  ~mock_cluster();

  void send_to(const rft::cluster_node &from, const rft::cluster_node &to,
               const rft::append_entries &m);

  void send_all(const rft::cluster_node &from, const rft::append_entries &m);

  void add_new(const rft::cluster_node &addr, const std::shared_ptr<rft::consensus> &c);

  std::vector<std::shared_ptr<rft::consensus>>
  by_filter(std::function<bool(const std::shared_ptr<rft::consensus>)> pred);

  void apply(std::function<void(const std::shared_ptr<rft::consensus>)> f);

  void on_heartbeat();

  void print_cluster();

  void erase_if(std::function<bool(const std::shared_ptr<rft::consensus>)> pred);

  size_t size() override;

protected:
  void worker();

private:
  std::thread _worker_thread;
  bool _stop_flag = false;
  bool _is_worker_active = false;
  std::mutex _tasks_locker;
  std::condition_variable _cond;
  /// from, to, message
  std::deque<message_t> _tasks;

  mutable std::shared_mutex _cluster_locker;
  std::map<rft::cluster_node, std::shared_ptr<rft::consensus>> _cluster;
};
