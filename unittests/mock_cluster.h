#pragma once

#include <librft/abstract_cluster.h>
#include <librft/consensus.h>
#include <condition_variable>
#include <map>
#include <shared_mutex>
#include <tuple>

class mock_cluster final : public rft::abstract_cluster {
  struct message_t {
    rft::cluster_node from;
    rft::cluster_node to;
    rft::append_entries m;
  };

public:
  mock_cluster();

  ~mock_cluster();

  void send_to(const rft::cluster_node &from,
               const rft::cluster_node &to,
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

  void wait_leader_eletion();

protected:
  void worker();

private:
  std::vector<std::thread> _worker_thread;
  volatile bool _stop_flag = false;
  std::atomic_size_t _is_worker_active = {0};
  std::mutex _tasks_locker;
  std::condition_variable _cond;
  /// from, to, message
  std::deque<message_t> _tasks;

  mutable std::shared_mutex _cluster_locker;
  std::map<rft::cluster_node, std::shared_ptr<rft::consensus>> _cluster;
};

inline bool is_leader_pred(const std::shared_ptr<rft::consensus> &v) {
  return v->state() == rft::NODE_KIND::LEADER;
};