#pragma once

#include <librft/abstract_cluster.h>
#include <librft/raft.h>
#include <condition_variable>
#include <shared_mutex>
#include <tuple>
#include <deque>
#include <unordered_map>
#include <unordered_set>

struct message_t {
  rft::node_name from;
  rft::node_name to;
  rft::append_entries m;
};

class worker_t {
  std::mutex _tasks_locker;
  volatile bool _stop_flag = false;
  std::shared_ptr<rft::raft> _target;
  std::condition_variable _cond;

  std::thread _tread;
  bool _is_node_stoped = false;

  volatile bool _is_stoped = false;
  rft::node_name self_addr;

public:
  worker_t(std::shared_ptr<rft::raft> t);
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

  void send_to(const rft::node_name &from,
               const rft::node_name &to,
               const rft::append_entries &m) override;

  void send_all(const rft::node_name &from, const rft::append_entries &m) override;
  size_t size() override;
  std::vector<rft::node_name> all_nodes() const override;

  void add_new(const rft::node_name &addr, const std::shared_ptr<rft::raft> &c);

  std::vector<std::shared_ptr<rft::raft>>
  by_filter(std::function<bool(const std::shared_ptr<rft::raft>)> pred);

  std::vector<std::shared_ptr<rft::raft>> get_all();

  void apply(std::function<void(const std::shared_ptr<rft::raft>)> f);

  void heartbeat();

  void print_cluster();

  void erase_if(std::function<bool(const std::shared_ptr<rft::raft>)> pred);

  void wait_leader_eletion(size_t max_leaders = 1);
  bool is_leader_eletion_complete(size_t max_leaders = 1);

  void stop_node(const rft::node_name &addr);
  void restart_node(const rft::node_name &addr);

  std::shared_ptr<mock_cluster> split(size_t count_to_move);
  void union_with(std::shared_ptr<mock_cluster> other);
protected:
  void stop_workers();
  void start_workers();
  void update_size();

private:
  mutable std::shared_mutex _cluster_locker;
  std::unordered_map<rft::node_name, std::shared_ptr<rft::raft>> _cluster;
  std::unordered_map<rft::node_name, std::shared_ptr<worker_t>> _workers;
  std::unordered_set<rft::node_name> _stoped;

  size_t _size = 0;
};

inline bool is_leader_pred(const std::shared_ptr<rft::raft> &v) {
  return v->kind() == rft::NODE_KIND::LEADER;
};

inline bool is_follower_pred(const std::shared_ptr<rft::raft> &v) {
  return v->kind() == rft::NODE_KIND::FOLLOWER;
};