#pragma once

#include <solidarity/client_exception.h>
#include <solidarity/error_codes.h>
#include <solidarity/event.h>
#include <solidarity/utils/utils.h>

#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <variant>
#include <vector>

namespace solidarity {

class async_result_t {
public:
  async_result_t(uint64_t id_) {
    _id = id_;
    answer_received = false;
  }

  void set_callback(std::function<void(ERROR_CODE)> callback) { _callback = callback; }

  void wait() {
    if (answer_received) {
      return;
    }
    std::unique_lock ul(_mutex);
    while (true) {
      _condition.wait(ul, [this] { return answer_received.load(); });
      if (answer_received) {
        break;
      }
    }
  }
  template <class T>
  [[nodiscard]] T await_result() {
    wait();
    if (err.empty()) {
      return std::get<T>(_answer);
    }
    throw solidarity::exception(err);
  }

  [[nodiscard]] std::vector<uint8_t> result() {
    return await_result<std::vector<uint8_t>>();
  }

  [[nodiscard]] cluster_state_event_t cluster_state() {
    return await_result<cluster_state_event_t>();
  }

  [[nodiscard]] cluster_state_event_t result_cluster_state() {
    wait();
    if (err.empty()) {
      return std::get<cluster_state_event_t>(_answer);
    }
    throw solidarity::exception(err);
  }

  template <class T>
  void set_result(T &&r, solidarity::ERROR_CODE ec, const std::string &err_) {
    std::lock_guard l(_mutex);
    _answer = r;
    _ec = ec;
    err = err_;
    answer_received = true;
    _condition.notify_all();
    if (_callback != nullptr) {
      _callback(ec);
    }
  }

  [[nodiscard]] uint64_t id() const { return _id; }
  [[nodiscard]] solidarity::ERROR_CODE ecode() const {
    ENSURE(_ec != solidarity::ERROR_CODE::UNDEFINED);
    return _ec;
  }

  std::string owner() const { return _owner; }

private:
  uint64_t _id;
  std::condition_variable _condition;
  std::mutex _mutex;
  std::variant<std::vector<uint8_t>, cluster_state_event_t> _answer;
  std::string err;
  solidarity::ERROR_CODE _ec = solidarity::ERROR_CODE::UNDEFINED;
  std::atomic_bool answer_received;
  std::function<void(ERROR_CODE)> _callback;
  std::string _owner;
};

class async_result_handler {
public:
  std::shared_ptr<async_result_t> make_waiter();
  std::shared_ptr<async_result_t> get_waiter(uint64_t id) const;
  std::vector<std::shared_ptr<async_result_t>> get_waiter(const std::string &owner) const;

  void erase_waiter(const std::string &owner);
  void erase_waiter(uint64_t id);

  void clear(ERROR_CODE ec) {
    std::lock_guard l(_locker);
    for (auto &kv : _async_results) {
      kv.second->set_result(std::vector<uint8_t>(), ec, "");
    }
    _async_results.clear();
    _next_query_id.store(0);
  }

  uint64_t get_next_id() { return _next_query_id.fetch_add(1); }

private:
  mutable std::shared_mutex _locker;
  std::atomic_uint64_t _next_query_id = 0;
  std::unordered_map<uint64_t, std::shared_ptr<async_result_t>> _async_results;
};
} // namespace solidarity