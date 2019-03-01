#include "mock_cluster.h"
#include <libutils/logger.h>

mock_cluster::mock_cluster() {
  _worker_thread = std::thread([this]() { this->worker(); });
}

mock_cluster::~mock_cluster() {
  utils::logging::logger_info("~ mock_cluster ");
  while (_is_worker_active) {
    _stop_flag = true;
    _cond.notify_all();
  }
  _worker_thread.join();
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
  apply([](auto n) {
    utils::logging::logger_info("?: ", n->self_addr(), "{", n->state(), ":", n->round(),
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
  return _cluster.size();
}

void mock_cluster::worker() {
  _is_worker_active = true;
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

        target->recv(v.from, v.m);
      }
      local_copy.clear();
    }
    _is_worker_active = false;
  } catch (std::exception &ex) {
    utils::logging::logger_fatal("mock_cluster: worker error:", ex.what());
  }
}