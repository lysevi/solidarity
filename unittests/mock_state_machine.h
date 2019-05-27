#include <mutex>
#include <solidarity/raft.h>

class mock_state_machine final : public solidarity::abstract_state_machine {
public:
  mock_state_machine(bool can_apply = true)
      : _can_apply(can_apply) {}

  void apply_cmd(const solidarity::command &cmd) override {
    std::lock_guard l(_locker);
    last_cmd = cmd;
  }
  void reset() override {
    std::lock_guard l(_locker);
    last_cmd.data.clear();
  }

  solidarity::command snapshot() override {
    std::shared_lock l(_locker);
    return last_cmd;
  }

  void install_snapshot(const solidarity::command &cmd) override {
    std::lock_guard l(_locker);
    last_cmd = cmd;
  }

  solidarity::command read(const solidarity::command &cmd) override {
    std::shared_lock l(_locker);
    if (last_cmd.is_empty()) {
      return cmd;
    }

    solidarity::command result = last_cmd;
    for (size_t i = 0; i < result.size(); ++i) {
      result.data[i] += cmd.data[0];
    }
    return result;
  }

  bool can_apply(const solidarity::command &) override { return _can_apply; }

  solidarity::command get_last_cmd() {
    std::shared_lock l(_locker);
    solidarity::command res = last_cmd;
    return res;
  }

  bool get_can_apply() {
    std::shared_lock l(_locker);
    return _can_apply;
  }

  void set_can_apply(bool f) {
    std::lock_guard l(_locker);
    _can_apply = f;
  }
private:
  std::shared_mutex _locker;
  solidarity::command last_cmd;
  bool _can_apply;
};