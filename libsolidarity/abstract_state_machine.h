#pragma once
#include <libsolidarity/command.h>
#include <libsolidarity/journal.h>

namespace solidarity {
class abstract_state_machine {
public:
  virtual ~abstract_state_machine() {}
  void add_reccord(const logdb::log_entry &le) {
    switch (le.kind) {
    case logdb::LOG_ENTRY_KIND::APPEND: {
      apply_cmd(le.cmd);
      break;
    }
    case logdb::LOG_ENTRY_KIND::SNAPSHOT: {
      install_snapshot(le.cmd);
      break;
    }
    }
  }
  virtual void apply_cmd(const command &cmd) = 0;
  virtual void reset() = 0;
  virtual command snapshot() = 0;
  virtual void install_snapshot(const command &cmd) = 0;
  virtual command read(const command &cmd) = 0;
  virtual bool can_apply(const command &) { return true; };
};
} // namespace solidarity