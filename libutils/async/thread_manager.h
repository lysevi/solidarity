#pragma once

#include <libutils/async/thread_pool.h>
#include <libutils/utils.h>
#include <libutils/utils_exports.h>
#include <shared_mutex>
#include <unordered_map>

namespace utils::async {
class thread_manager final : public utils::non_copy {

public:
  struct params_t {
    std::vector<threads_pool::params_t> pools;
    params_t(std::vector<threads_pool::params_t> _pools) { pools = _pools; }
  };
  EXPORT thread_manager(const params_t &params);
  EXPORT ~thread_manager();
  EXPORT void stop();
  EXPORT void flush();
  // task_result_ptr post(const THREAD_KINDS kind,
  //                    const std::shared_ptr<async_task_wrapper> &task) {
  //  return this->post((thread_kind_t)kind, task);
  //}
  EXPORT task_result_ptr post(const thread_kind_t kind, const task_wrapper_ptr &task);

  size_t active_works() {
    size_t res = 0;
    for (const auto &kv : _pools) {
      res += kv.second->active_workers();
    }
    return res;
  }

private:
private:
  bool _stoping_begin = false;
  bool _stoped = false;
  params_t _params;
  std::shared_mutex _locker;
  std::unordered_map<thread_kind_t, std::shared_ptr<threads_pool>> _pools;
};
} // namespace utils::async