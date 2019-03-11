#pragma once

#include <librft/abstract_cluster.h>
#include <librft/consensus.h>
#include <condition_variable>
#include <shared_mutex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

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

  void heartbeat();

  void print_cluster();

  void erase_if(std::function<bool(const std::shared_ptr<rft::consensus>)> pred);

  size_t size() override;

  void wait_leader_eletion(size_t max_leaders=1);
  bool is_leader_eletion_complete(size_t max_leaders=1);

  void stop_node(const rft::cluster_node &addr);
  void restart_node(const rft::cluster_node &addr);

  std::shared_ptr<mock_cluster> split(size_t count_to_move);
  void union_with(std::shared_ptr<mock_cluster> other);
protected:
  void worker();

  void stop_workers();
  void start_workers();

private:
  std::vector<std::thread> _worker_thread;
  size_t _worker_thread_count = 0;
  volatile bool _stop_flag = false;
  std::atomic_size_t _is_worker_active = {0};
  std::mutex _tasks_locker;
  std::condition_variable _cond;
  /// from, to, message
  std::deque<message_t> _tasks;

  mutable std::shared_mutex _cluster_locker;
  std::unordered_map<rft::cluster_node, std::shared_ptr<rft::consensus>> _cluster;
  std::unordered_set<rft::cluster_node> _stoped;
};

inline bool is_leader_pred(const std::shared_ptr<rft::consensus> &v) {
  return v->state() == rft::NODE_KIND::LEADER;
};

inline bool is_follower_pred(const std::shared_ptr<rft::consensus> &v) {
  return v->state() == rft::NODE_KIND::FOLLOWER;
};