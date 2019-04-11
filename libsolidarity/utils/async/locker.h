#pragma once

#include <atomic>
#include <mutex>
#include <thread>

namespace utils::async {
template <size_t LOCKER_MAX_TRY>
class spin_lock {
  std::atomic_flag locked;

public:
  spin_lock()
      : locked() {}
  bool try_lock() noexcept {
    for (size_t num_try = 0; num_try < LOCKER_MAX_TRY; ++num_try) {
      if (!locked.test_and_set(std::memory_order_acquire)) {
        return true;
      }
    }
    return false;
  }

  void lock() noexcept {
    while (!try_lock()) {
      std::this_thread::yield();
    }
  }

  void unlock() noexcept { locked.clear(std::memory_order_release); }
};

using locker = spin_lock<10>;
using locker_ptr = std::shared_ptr<utils::async::locker>;
} // namespace utils::async