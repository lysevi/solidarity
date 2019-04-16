#pragma once
#include <iostream>
#include <solidarity/abstract_state_machine.h>
#include <shared_mutex>

namespace common_inner {
union converted {
  uint64_t value;
  unsigned char values[8];
};

uint64_t cmd2int(const std::vector<uint8_t> &data) {
  if (data.size() < sizeof(uint64_t)) {
    throw std::logic_error("cmd.data.size() < sizeof(uint64_t)");
  }
  converted c;
  for (size_t i = 0; i < sizeof(uint64_t); ++i) {
    c.values[i] = data[i];
  }
  return c.value;
}

uint64_t cmd2int(const solidarity::command &cmd) {
  return cmd2int(cmd.data);
}

solidarity::command int2cmd(const uint64_t v) {
  converted c;
  c.value = v;
  solidarity::command result;
  result.data.resize(sizeof(uint64_t));

  for (size_t i = 0; i < sizeof(uint64_t); ++i) {
    result.data[i] = c.values[i];
  }
  return result;
}
} // namespace common_inner

class distr_inc_sm final : public solidarity::abstract_state_machine {
public:
  distr_inc_sm()
      : counter(0) {}

  void apply_cmd(const solidarity::command &cmd) override {
    std::lock_guard l(_locker);
    counter = common_inner::cmd2int(cmd);
    std::cout << "add counter: " << counter << std::endl;
  }

  void reset() override {
    std::lock_guard l(_locker);
    counter = 0;
    std::cout << "reset counter: " << counter << std::endl;
  }

  solidarity::command snapshot() override {
    std::shared_lock l(_locker);
    return common_inner::int2cmd(counter);
  }

  void install_snapshot(const solidarity::command &cmd) override {
    std::lock_guard l(_locker);
    counter = common_inner::cmd2int(cmd);
    std::cout << "install_snapshot counter: " << counter << std::endl;
  }

  solidarity::command read(const solidarity::command & /*cmd*/) override {
    return snapshot();
  }

  bool can_apply(const solidarity::command &) override { return true; }

  std::shared_mutex _locker;
  uint64_t counter;
};