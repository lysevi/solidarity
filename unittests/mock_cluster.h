#pragma once

#include <librft/abstract_cluster.h>
#include <librft/consensus.h>
#include <condition_variable>
#include <shared_mutex>
#include <tuple>
#include <deque>
#include <unordered_map>
#include <unordered_set>

struct message_t {
  rft::cluster_node from;
  rft::cluster_node to;
  rft::append_entries m;
};

class worker_t {
  std::mutex _tasks_locker;
  volatile bool _stop_flag = false;
  std::shared_ptr<rft::consensus> _target;
  std::condition_variable _cond;

  std::thread _tread;
  bool _is_node_stoped = false;

  volatile bool _is_stoped = false;
  rft::cluster_node self_addr;

public:
  worker_t(std::shared_ptr<rft::consensus> t);
  ~worker_t();
  void add_task(const message_t &mt);
  void worker();
  void stop();

  std::deque<message_t> _tasks;
};

class mock_cluster final : public rft::abstract_cluster {

public:
  mock_cluster();

  ~mock_cluster() override;

  void send_to(const rft::cluster_node &from,
               const rft::cluster_node &to,
               const rft::append_entries &m) override;

  void send_all(const rft::cluster_node &from, const rft::append_entries &m) override;
  size_t size() override;
  std::vector<rft::cluster_node> all_nodes() const override;

  void add_new(const rft::cluster_node &addr, const std::shared_ptr<rft::consensus> &c);

  std::vector<std::shared_ptr<rft::consensus>>
  by_filter(std::function<bool(const std::shared_ptr<rft::consensus>)> pred);

  std::vector<std::shared_ptr<rft::consensus>> get_all();

  void apply(std::function<void(const std::shared_ptr<rft::consensus>)> f);

  void heartbeat();

  void print_cluster();

  void erase_if(std::function<bool(const std::shared_ptr<rft::consensus>)> pred);

  void wait_leader_eletion(size_t max_leaders = 1);
  bool is_leader_eletion_complete(size_t max_leaders = 1);

  void stop_node(const rft::cluster_node &addr);
  void restart_node(const rft::cluster_node &addr);

  std::shared_ptr<mock_cluster> split(size_t count_to_move);
  void union_with(std::shared_ptr<mock_cluster> other);

protected:
  void stop_workers();
  void start_workers();
  void update_size();

private:
  mutable std::shared_mutex _cluster_locker;
  std::unordered_map<rft::cluster_node, std::shared_ptr<rft::consensus>> _cluster;
  std::unordered_map<rft::cluster_node, std::shared_ptr<worker_t>> _workers;
  std::unordered_set<rft::cluster_node> _stoped;

  size_t _size = 0;
};

inline bool is_leader_pred(const std::shared_ptr<rft::consensus> &v) {
  return v->state() == rft::NODE_KIND::LEADER;
};

inline bool is_follower_pred(const std::shared_ptr<rft::consensus> &v) {
  return v->state() == rft::NODE_KIND::FOLLOWER;
};