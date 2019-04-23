#pragma once

#include <solidarity/client_exception.h>
#include <solidarity/error_codes.h>
#include <solidarity/utils/utils.h>

#include <condition_variable>
#include <mutex>

namespace solidarity {

class async_result_t {
public:
  solidarity::async_result_t(uint64_t id_) {
    _id = id_;
    answer_received = false;
  }

  void wait() {
    std::unique_lock ul(_mutex);
    while (true) {
      _condition.wait(ul, [this] { return answer_received; });
      if (answer_received) {
        break;
      }
    }
  }
  [[nodiscard]] std::vector<uint8_t> result() {
    wait();
    if (err.empty()) {
      return answer;
    }
    throw solidarity::exception(err);
  }

  void set_result(const std::vector<uint8_t> &r,
                  solidarity::ERROR_CODE ec,
                  const std::string &err_) {
    answer = r;
    _ec = ec;
    err = err_;
    answer_received = true;
    _condition.notify_all();
  }
  [[nodiscard]] uint64_t id() const { return _id; }
  [[nodiscard]] solidarity::ERROR_CODE ecode() const {
    ENSURE(_ec != solidarity::ERROR_CODE::UNDEFINED);
    return _ec;
  }

private:
  uint64_t _id;
  std::condition_variable _condition;
  std::mutex _mutex;
  std::vector<uint8_t> answer;
  std::string err;
  solidarity::ERROR_CODE _ec = solidarity::ERROR_CODE::UNDEFINED;
  bool answer_received;
};
} // namespace solidarity