#pragma once
#include <libsolidarity/command.h>

namespace solidarity {
class abstract_state_machine {
public:
  virtual ~abstract_state_machine() {}
  virtual void apply_cmd(const command &cmd) = 0;
  virtual void reset() = 0;
  virtual command snapshot() = 0;
  virtual void install_snapshot(const command &cmd) = 0;
  virtual command read(const command &cmd) = 0;
  virtual bool can_apply(const command &) { return true; };
};
} // namespace solidarity