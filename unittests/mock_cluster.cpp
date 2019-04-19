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
  /* try {
     _tread.join();
   } catch (...) {
   }*/
  /* while (_tread.joinable()) {
     std::this_thread::yield();
   }*/
}

void worker_t::add_task(const message_t &mt) {
  ENSURE(!self_addr.is_empty());
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
  _size = _cluster.size() - _stoped.size();
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
  std::shared_lock lg(_cluster_locker);
  ENSURE(!from.is_empty());
  for (const auto &kv : _cluster) {
    if (kv.first != from) {
      message_t me{from, kv.first, m};
      _workers[kv.first]->add_task(me);
    }
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
  std::shared_lock lg(_cluster_locker);
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
  std::shared_lock lg(_cluster_locker);
  for (const auto &kv : _cluster) {
    f(kv.second);
  }
}

void mock_cluster::heartbeat() {
  apply([](auto n) { return n->heartbeat(); });
}

void mock_cluster::print_cluster() {
  solidarity::utils::logging::logger_info("----------------------------");
  /*std::cout << "----------------------------\n";*/
  apply([](auto n) {
    /*std::cout << solidarity::utils::strings::args_to_string("?: ", n->self_addr(), "{",
       n->kind(),
                                                ":", n->term(), "}", " => ",
                                                n->get_leader(), "\n");*/
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
  }
  if (!target.is_empty()) {
    std::shared_lock lg(_cluster_locker);
    for (auto &kv : _cluster) {
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
  if (_cluster.size() != _stoped.size()) {
    result.reserve(_cluster.size() - _stoped.size());
    for (const auto &kv : _cluster) {
      if (_stoped.find(kv.first) == _stoped.end()) {
        result.push_back(kv.first);
      }
    }
  }
  return result;
}

void mock_cluster::wait_leader_eletion(size_t max_leaders) {
  while (true) {
    if (is_leader_eletion_complete(max_leaders)) {
      break;
    }
    heartbeat();
    print_cluster();
  }
}

bool mock_cluster::is_leader_eletion_complete(size_t max_leaders) {
  const auto leaders = by_filter(is_leader_pred);
  if (leaders.size() > max_leaders) {
    solidarity::utils::logging::logger_fatal("raft error!!!");
    print_cluster();
    throw std::logic_error("raft error");
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

void mock_cluster::stop_node(const solidarity::node_name &addr) {
  std::lock_guard<std::shared_mutex> lg(_cluster_locker);
  _stoped.insert(addr);
  update_size();
}

void mock_cluster::restart_node(const solidarity::node_name &addr) {
  std::lock_guard<std::shared_mutex> lg(_cluster_locker);
  _stoped.erase(addr);
  update_size();
}

std::shared_ptr<mock_cluster> mock_cluster::split(size_t count_to_move) {
  auto result = std::make_shared<mock_cluster>();
  // std::lock_guard<std::shared_mutex> lg_res(result->_cluster_locker);
  stop_workers();
  result->stop_workers();

  for (size_t i = 0; i < count_to_move; ++i) {
    if (_cluster.size() == 0) {
      break;
    }
    auto it = _cluster.begin();
    result->add_new(it->first, it->second);

    _cluster.erase(it);
    update_size();

    std::deque<message_t> &_tasks = _workers[it->first]->_tasks;
    _tasks.erase(std::remove_if(_tasks.begin(),
                                _tasks.end(),
                                [it](const message_t &m) -> bool {
                                  return m.from == it->first || m.to == it->first;
                                }),
                 _tasks.end());
  }
  for (auto &rkv : result->_cluster) {
    for (auto &kv : _cluster) {
      kv.second->lost_connection_with(rkv.first);
      rkv.second->lost_connection_with(kv.first);
    }
  }

  start_workers();
  result->start_workers();
  return result;
}

void mock_cluster::union_with(std::shared_ptr<mock_cluster> other) {
  other->stop_workers();
  stop_workers();
  for (auto &&kv : other->_cluster) {
    kv.second->set_cluster(this);
    _cluster.insert(std::move(kv));
  }
  update_size();
  other->_cluster.clear();
  start_workers();
}
