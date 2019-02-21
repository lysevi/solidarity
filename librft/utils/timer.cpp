#include <librft/utils/timer.h>
#include <librft/utils/utils.h>
using namespace rft::utils;

void timer_t::start() {
  if (!_started) {
    _stoped = false;
    _started = true;
    _t = std::thread([this]() { _worker(); });
  }
}

void timer_t::stop() {
  _stoped = true;
  _t.join();
}

void timer_t::restart() {
  stop();
  start();
}

void timer_t::_worker() {
  auto start_time = std::chrono::system_clock::now();
  auto end_time = start_time + _duration;
  while (!_stoped) {
    sleep_mls(100);
    auto cur_time = std::chrono::system_clock::now();
    if (cur_time > end_time) {
      _callback();
      if (!_cyclic) {
        break;
      }
      end_time = cur_time + _duration;
    }
  }
  _started = false;
}