#include "mock_cluster.h"
#include <algorithm>
#include <cassert>
#include <solidarity/utils/logger.h>
#include <solidarity/utils/utils.h>

worker_t::worker_t(std::shared_ptr<solidarity::raft> t) {
  _target = t;
  self_addr = _target->self_addr();
  _tread = std::thread([this]() { this->worker(); });
}

worker_t::~worker_t() {}

void worker_t::stop() {
  while (!_is_stoped) {
    _stop_flag = true;
    _cond.notify_all();
  }
  _tread.join();
}

void worker_t::add_task(const message_t &mt) {
  ENSURE(!self_addr.empty());
  ENSURE(mt.from != self_addr);
  {
    std::lock_guard l(_tasks_locker);
    ENSURE(mt.to == self_addr);
    _tasks.push_back(mt);
  }
  _cond.notify_all();
}

void worker_t::worker() {
  try {
    while (!_stop_flag) {
      std::vector<message_t> local_copy;
      {
        std::unique_lock<std::mutex> ul(_tasks_locker);
        _cond.wait(ul, [this] { return this->_stop_flag || !this->_tasks.empty(); });
        if (_stop_flag) {
          break;
        }
        if (_tasks.empty()) {
          continue;
        }
        local_copy.reserve(_tasks.size());
        std::copy(_tasks.begin(), _tasks.end(), std::back_inserter(local_copy));
        _tasks.clear();
      }
      for (auto &&v : local_copy) {
        if (!_is_node_stoped) {
          _target->recv(v.from, v.m);
        }
      }
      local_copy.clear();
    }

  } catch (std::exception &ex) {
    solidarity::utils::logging::logger_fatal(
        "mock_cluster: worker ", _target->self_addr(), " error:", ex.what());
    std::exit(1);
  }
  _is_stoped = true;
}

mock_cluster::mock_cluster() {}

mock_cluster::~mock_cluster() {
  solidarity::utils::logging::logger_info("~ mock_cluster ");
  stop_workers();
}

void mock_cluster::update_size() {
  _size = _cluster.size();
}

void mock_cluster::start_workers() {
  std::lock_guard<std::shared_mutex> l(_cluster_locker);
  for (auto &kv : _cluster) {
    _workers[kv.first] = std::shared_ptr<worker_t>();
  }
}

void mock_cluster::stop_workers() {
  {
    std::shared_lock l(_cluster_locker);
    for (auto &v : _workers) {
      v.second->stop();
    }
  }
  std::lock_guard<std::shared_mutex> l(_cluster_locker);
  _workers.clear();
}

void mock_cluster::send_to(const solidarity::node_name &from,
                           const solidarity::node_name &to,
                           const solidarity::append_entries &m) {
  std::shared_lock ul(_cluster_locker);

  if (auto it = _workers.find(to); it != _workers.end()) {
    _workers[to]->add_task(message_t{from, to, m});
  }
}

void mock_cluster::send_all(const solidarity::node_name &from,
                            const solidarity::append_entries &m) {
  std::unordered_map<solidarity::node_name, std::shared_ptr<worker_t>> cp;
  {
    std::shared_lock lg(_cluster_locker);
    ENSURE(!from.empty());
    cp.reserve(_cluster.size());
    for (const auto &kv : _workers) {
      if (kv.first != from) {
        cp.insert(kv);
      }
    }
  }

  for (const auto &kv : cp) {
    message_t me{from, kv.first, m};
    kv.second->add_task(me);
  }
}

void mock_cluster::add_new(const solidarity::node_name &addr,
                           const std::shared_ptr<solidarity::raft> &c) {
  std::lock_guard<std::shared_mutex> lg(_cluster_locker);
  if (_workers.find(addr) != _workers.end()) {
    THROW_EXCEPTION("_workers.find(addr) != _workers.end()");
  }
  _workers[addr] = std::make_shared<worker_t>(c);
  c->set_cluster(this);
  _cluster[addr] = c;
  _size++;
}

std::vector<std::shared_ptr<solidarity::raft>> mock_cluster::by_filter(
    std::function<bool(const std::shared_ptr<solidarity::raft>)> pred) {
  // std::shared_lock lg(_cluster_locker);
  std::vector<std::shared_ptr<solidarity::raft>> result;
  result.reserve(_cluster.size());
  for (const auto &kv : _cluster) {
    if (pred(kv.second)) {
      result.push_back(kv.second);
    }
  }
  return result;
}

std::vector<std::shared_ptr<solidarity::raft>> mock_cluster::get_all() {
  return by_filter([](auto) { return true; });
}

void mock_cluster::apply(std::function<void(const std::shared_ptr<solidarity::raft>)> f) {
  std::vector<std::shared_ptr<solidarity::raft>> cp;
  {
    std::shared_lock lg(_cluster_locker);
    cp.resize(_cluster.size());
    std::transform(_cluster.cbegin(), _cluster.cend(), cp.begin(), [](auto kv) {
      return kv.second;
    });
  }
  for (const auto &v : cp) {
    f(v);
  }
}

void mock_cluster::heartbeat() {
  apply([](auto n) { return n->heartbeat(); });
}

void mock_cluster::print_cluster() {
  solidarity::utils::logging::logger_info("----------------------------");
  apply([](auto n) {
    solidarity::utils::logging::logger_info("?: ",
                                            n->self_addr(),
                                            "{",
                                            n->kind(),
                                            ":",
                                            n->term(),
                                            "}",
                                            " => ",
                                            n->get_leader());
  });
}

void mock_cluster::erase_if(
    std::function<bool(const std::shared_ptr<solidarity::raft>)> pred) {

  solidarity::node_name target;
  std::unordered_map<solidarity::node_name, std::shared_ptr<solidarity::raft>> cp_cluster;
  {
    _cluster_locker.lock_shared();
    auto it = std::find_if(
        _cluster.begin(), _cluster.end(), [pred](auto kv) { return pred(kv.second); });
    if (it == _cluster.end()) {
      _cluster_locker.unlock_shared();
      return;
    }
    target = it->first;
    _workers[target]->stop();

    _cluster_locker.unlock_shared();

    std::lock_guard<std::shared_mutex> l(_cluster_locker);
    _cluster.erase(target);
    _workers.erase(target);
    update_size();
    cp_cluster.reserve(_cluster.size());
    for (auto &kv : _cluster) {
      cp_cluster.insert(kv);
    }
  }
  if (!target.empty()) {
    for (auto &kv : cp_cluster) {
      kv.second->lost_connection_with(target);
    }
  }
}

size_t mock_cluster::size() {
  return _size;
}

std::vector<solidarity::node_name> mock_cluster::all_nodes() const {
  std::shared_lock lg(_cluster_locker);
  std::vector<solidarity::node_name> result;

  result.reserve(_cluster.size());
  for (const auto &kv : _cluster) {
    result.push_back(kv.first);
  }

  return result;
}

void mock_cluster::wait_leader_eletion(size_t max_leaders) {
  while (true) {
    if (is_leader_election_complete(max_leaders)) {
      break;
    }
    heartbeat();
    print_cluster();
  }
}

bool mock_cluster::is_leader_election_complete(size_t max_leaders) {
  const auto leaders = by_filter(is_leader_pred);
  if (leaders.size() > max_leaders) {
    solidarity::utils::logging::logger_fatal("raft error!!!");
    print_cluster();
    // throw std::logic_error("raft error");
    return false;
  }
  if (leaders.size() == 1) {
    auto cur_leader = leaders.front()->self_addr();
    auto followers
        = by_filter([cur_leader](const std::shared_ptr<solidarity::raft> &v) -> bool {
            auto nkind = v->state().node_kind;
            return v->get_leader() == cur_leader
                   && (nkind == solidarity::NODE_KIND::LEADER
                       || nkind == solidarity::NODE_KIND::FOLLOWER);
          });
    if (followers.size() == size()) {
      return true;
    }
  }
  return false;
}
