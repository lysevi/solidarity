#pragma once
#include <solidarity/command.h>

namespace solidarity {
class abstract_state_machine {
public:
  virtual ~abstract_state_machine() {}
  virtual void apply_cmd(const command_t &cmd) = 0;
  virtual void reset() = 0;
  virtual command_t snapshot() = 0;
  virtual void install_snapshot(const command_t &cmd) = 0;
  virtual command_t read(const command_t &cmd) = 0;
  virtual bool can_apply(const command_t &) { return true; }
};
} // namespace solidarity