#include <solidarity/async_result.h>
#include <solidarity/queries.h>

using namespace solidarity;

std::shared_ptr<async_result_t> async_result_handler::make_waiter() {
  std::lock_guard l(_locker);
  auto waiter = std::make_shared<async_result_t>(_next_query_id.fetch_add(1));
  _async_results[waiter->id()] = waiter;
  return waiter;
}

std::shared_ptr<async_result_t> async_result_handler::get_waiter(uint64_t id) const {
  std::shared_lock l(_locker);
  if (auto it = _async_results.find(id); it != _async_results.end()) {
    return it->second;
  }
  THROW_EXCEPTION("async result for id:", id, " not found!");
}

void async_result_handler::erase_waiter(uint64_t id) {
  std::lock_guard l(_locker);
  if (auto it = _async_results.find(id); it != _async_results.end()) {
    _async_results.erase(it);
  }
}