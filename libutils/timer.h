#pragma once

#include <libutils/utils_exports.h>
#include <chrono>
#include <functional>
#include <thread>

namespace utils {
class timer_t {
public:
  timer_t(const std::chrono::milliseconds &duration,
          std::function<void()> callback,
          bool cyclic = true)
      : _duration(duration)
      , _callback(callback)
      , _cyclic(cyclic) {}

  ~timer_t() { stop(); }

  EXPORT void start();
  EXPORT void stop();
  EXPORT void restart();
  EXPORT bool is_started() const { return _started; }

private:
  void _worker();

protected:
  std::chrono::milliseconds _duration;
  std::function<void()> _callback;
  std::thread _t;
  bool _cyclic;
  volatile bool _started = false;
  volatile bool _stoped = true;
};
} // namespace utils
