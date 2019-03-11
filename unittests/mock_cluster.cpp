#include "mock_cluster.h"
#include <libutils/logger.h>
#include <algorithm>

mock_cluster::mock_cluster() {}

mock_cluster::~mock_cluster() {
  utils::logging::logger_info("~ mock_cluster ");
  if (!_worker_thread.empty()) {
    stop_workers();
  }
}

void mock_cluster::start_workers() {
  _stop_flag = false;
  for (size_t i = 0; i < _worker_thread_count; ++i) {
    _worker_thread.emplace_back(std::thread([this]() { this->worker(); }));
  }
}

void mock_cluster::stop_workers() {
  while (_is_worker_active.load() != size_t(0)) {
    _stop_flag = true;
    _cond.notify_all();
  }
  for (auto &&t : _worker_thread) {
    t.join();
  }
  _worker_thread.clear();
}

void mock_cluster::send_to(const rft::cluster_node &from,
                           const rft::cluster_node &to,
                           const rft::append_entries &m) {
  std::unique_lock<std::mutex> ul(_tasks_locker);
  _tasks.emplace_back<mock_cluster::message_t>({from, to, m});
  _cond.notify_all();
}

void mock_cluster::send_all(const rft::cluster_node &from, const rft::append_entries &m) {
  std::unique_lock<std::mutex> lg(_tasks_locker);
  for (const auto &kv : _cluster) {
    if (kv.first != from) {
      _tasks.push_back({from, kv.first, m});
    }
  }
  _cond.notify_all();
}

void mock_cluster::add_new(const rft::cluster_node &addr,
                           const std::shared_ptr<rft::consensus> &c) {
  std::lock_guard<std::shared_mutex> lg(_cluster_locker);
  if (_worker_thread.size() < 5) {
    _worker_thread.emplace_back(std::thread([this]() { this->worker(); }));
    _worker_thread_count++;
  }
  c->set_cluster(this);
  _cluster[addr] = c;
}

std::vector<std::shared_ptr<rft::consensus>>
mock_cluster::by_filter(std::function<bool(const std::shared_ptr<rft::consensus>)> pred) {
  std::shared_lock<std::shared_mutex> lg(_cluster_locker);
  std::vector<std::shared_ptr<rft::consensus>> result;
  result.reserve(_cluster.size());
  for (const auto &kv : _cluster) {
    if (pred(kv.second)) {
      result.push_back(kv.second);
    }
  }
  return result;
}

void mock_cluster::apply(std::function<void(const std::shared_ptr<rft::consensus>)> f) {
  std::shared_lock<std::shared_mutex> lg(_cluster_locker);
  for (const auto &kv : _cluster) {
    f(kv.second);
  }
}

void mock_cluster::on_heartbeat() {
  apply([](auto n) { return n->on_heartbeat(); });
}

void mock_cluster::print_cluster() {
  utils::logging::logger_info("----------------------------");
  /*std::cout << "----------------------------\n";*/
  apply([](auto n) {
    /*std::cout << utils::strings::args_to_string("?: ", n->self_addr(), "{", n->state(),
                                                ":", n->term(), "}", " => ",
                                                n->get_leader(), "\n");*/
    utils::logging::logger_info("?: ", n->self_addr(), "{", n->state(), ":", n->term(),
                                "}", " => ", n->get_leader());
  });
}

void mock_cluster::erase_if(
    std::function<bool(const std::shared_ptr<rft::consensus>)> pred) {
  std::lock_guard<std::shared_mutex> lg(_cluster_locker);
  auto it = std::find_if(_cluster.begin(), _cluster.end(),
                         [pred](auto kv) { return pred(kv.second); });
  if (it != _cluster.end()) {
    _cluster.erase(it);
  }
}

size_t mock_cluster::size() {
  std::shared_lock<std::shared_mutex> lg(_cluster_locker);
  return _cluster.size() - _stoped.size();
}

void mock_cluster::worker() {
  _is_worker_active++;
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
        std::shared_ptr<rft::consensus> target = nullptr;
        {
          std::shared_lock<std::shared_mutex> lg(_cluster_locker);
          auto it = _cluster.find(v.to);

          if (it == _cluster.cend()) {
            continue;
          } else {
            target = it->second;
          }
        }
        if (_stoped.find(v.to) == _stoped.end()) {
          target->recv(v.from, v.m);
        }
      }
      local_copy.clear();
    }

  } catch (std::exception &ex) {
    utils::logging::logger_fatal("mock_cluster: worker error:", ex.what());
  }
  _is_worker_active--;
}

void mock_cluster::wait_leader_eletion(size_t max_leaders) {
  while (true) {
    if (is_leader_eletion_complete(max_leaders)) {
      break;
    }
    on_heartbeat();
    print_cluster();
  }
}

bool mock_cluster::is_leader_eletion_complete(size_t max_leaders) {
  auto leaders = by_filter(is_leader_pred);
  if (leaders.size() > max_leaders) {
    utils::logging::logger_fatal("consensus error!!!");
    print_cluster();
    throw std::logic_error("consensus error");
  }
  if (leaders.size() == 1) {
    auto cur_leader = leaders.front()->self_addr();
    auto followers
        = by_filter([cur_leader](const std::shared_ptr<rft::consensus> &v) -> bool {
            return v->get_leader() == cur_leader;
          });
    if (followers.size() == size()) {
      return true;
    }
  }
  return false;
}

void mock_cluster::stop_node(const rft::cluster_node &addr) {
  std::lock_guard<std::shared_mutex> lg(_cluster_locker);
  _stoped.insert(addr);
}

void mock_cluster::restart_node(const rft::cluster_node &addr) {
  std::lock_guard<std::shared_mutex> lg(_cluster_locker);
  _stoped.erase(addr);
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
    _tasks.erase(std::remove_if(_tasks.begin(), _tasks.end(),
                                [it](const message_t &m) -> bool {
                                  return m.from == it->first || m.to == it->first;
                                }),
                 _tasks.end());
  }
  start_workers();
  result->start_workers();
  return result;
}

void mock_cluster::union_with(std::shared_ptr<mock_cluster> other) {
  other->stop_workers();
  stop_workers();
  for (auto &&kv : other->_cluster) {
    _cluster.insert(std::move(kv));
  }
  other->_cluster.clear();
  start_workers();
}