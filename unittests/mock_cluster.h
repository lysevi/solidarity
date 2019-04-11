#pragma once

#include <libsolidarity/abstract_cluster.h>
#include <libsolidarity/raft.h>
#include <condition_variable>
#include <shared_mutex>
#include <tuple>
#include <deque>
#include <unordered_map>
#include <unordered_set>

struct message_t {
  solidarity::node_name from;
  solidarity::node_name to;
  solidarity::append_entries m;
};

class worker_t {
  std::mutex _tasks_locker;
  volatile bool _stop_flag = false;
  std::shared_ptr<solidarity::raft> _target;
  std::condition_variable _cond;

  std::thread _tread;
  bool _is_node_stoped = false;

  volatile bool _is_stoped = false;
  solidarity::node_name self_addr;

public:
  worker_t(std::shared_ptr<solidarity::raft> t);
  ~worker_t();
  void add_task(const message_t &mt);
  void worker();
  void stop();

  std::deque<message_t> _tasks;
};

class mock_cluster final : public solidarity::abstract_cluster {

public:
  mock_cluster();

  ~mock_cluster() override;

  void send_to(const solidarity::node_name &from,
               const solidarity::node_name &to,
               const solidarity::append_entries &m) override;

  void send_all(const solidarity::node_name &from, const solidarity::append_entries &m) override;
  size_t size() override;
  std::vector<solidarity::node_name> all_nodes() const override;

  void add_new(const solidarity::node_name &addr, const std::shared_ptr<solidarity::raft> &c);

  std::vector<std::shared_ptr<solidarity::raft>>
  by_filter(std::function<bool(const std::shared_ptr<solidarity::raft>)> pred);

  std::vector<std::shared_ptr<solidarity::raft>> get_all();

  void apply(std::function<void(const std::shared_ptr<solidarity::raft>)> f);

  void heartbeat();

  void print_cluster();

  void erase_if(std::function<bool(const std::shared_ptr<solidarity::raft>)> pred);

  void wait_leader_eletion(size_t max_leaders = 1);
  bool is_leader_eletion_complete(size_t max_leaders = 1);

  void stop_node(const solidarity::node_name &addr);
  void restart_node(const solidarity::node_name &addr);

  std::shared_ptr<mock_cluster> split(size_t count_to_move);
  void union_with(std::shared_ptr<mock_cluster> other);
protected:
  void stop_workers();
  void start_workers();
  void update_size();

private:
  mutable std::shared_mutex _cluster_locker;
  std::unordered_map<solidarity::node_name, std::shared_ptr<solidarity::raft>> _cluster;
  std::unordered_map<solidarity::node_name, std::shared_ptr<worker_t>> _workers;
  std::unordered_set<solidarity::node_name> _stoped;

  size_t _size = 0;
};

inline bool is_leader_pred(const std::shared_ptr<solidarity::raft> &v) {
  return v->kind() == solidarity::NODE_KIND::LEADER;
};

inline bool is_follower_pred(const std::shared_ptr<solidarity::raft> &v) {
  return v->kind() == solidarity::NODE_KIND::FOLLOWER;
};