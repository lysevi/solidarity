#pragma once
#include <librft/command.h>

namespace rft{
class abstract_consensus_consumer {
public:
  virtual ~abstract_consensus_consumer() {}
  virtual void apply_cmd(const command &cmd) = 0;
  virtual void reset() = 0;
  virtual command snapshot() = 0;
  virtual command read(const command &cmd) = 0;
};
}