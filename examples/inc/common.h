#pragma once
#include <iostream>
#include <shared_mutex>
#include <solidarity/abstract_state_machine.h>

class distr_inc_sm final : public solidarity::abstract_state_machine {
public:
  distr_inc_sm()
      : counter(0) {}

  void apply_cmd(const solidarity::command &cmd) override {
    std::lock_guard l(_locker);
    counter = cmd.to_value<uint64_t>();
    std::cout << "add counter: " << counter << std::endl;
  }

  void reset() override {
    std::lock_guard l(_locker);
    counter = 0;
    std::cout << "reset counter: " << counter << std::endl;
  }

  solidarity::command snapshot() override {
    std::shared_lock l(_locker);
    return solidarity::command::from_value<uint64_t>(counter);
  }

  void install_snapshot(const solidarity::command &cmd) override {
    std::lock_guard l(_locker);
    counter = cmd.to_value<uint64_t>();
    std::cout << "install_snapshot counter: " << counter << std::endl;
  }

  solidarity::command read(const solidarity::command & /*cmd*/) override {
    return snapshot();
  }

  bool can_apply(const solidarity::command &) override { return true; }

  std::shared_mutex _locker;
  uint64_t counter;
};