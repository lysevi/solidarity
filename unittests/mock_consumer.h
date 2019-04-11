#include <librft/raft.h>
#include <mutex>

class mock_state_machine final : public rft::abstract_state_machine {
public:
  void apply_cmd(const rft::command &cmd) override {
    std::lock_guard l(_locker);
    last_cmd = cmd;
  }
  void reset() override {
    std::lock_guard l(_locker);
    last_cmd.data.clear();
  }

  rft::command snapshot() override {
    std::shared_lock l(_locker);
    return last_cmd;
  }

  rft::command read(const rft::command &cmd) override {
    std::shared_lock l(_locker);
    if (last_cmd.data.empty()) {
      return cmd;
    }

    rft::command result = last_cmd;
    for (size_t i = 0; i < result.data.size(); ++i) {
      result.data[i] += cmd.data[0];
    }
    return result;
  }

  std::shared_mutex _locker;
  rft::command last_cmd;
};