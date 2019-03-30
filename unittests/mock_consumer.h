#include <librft/consensus.h>

class mock_consumer final : public rft::abstract_consensus_consumer {
public:
  void apply_cmd(const rft::command &cmd) override { last_cmd = cmd; }
  void reset() override { last_cmd.data.clear(); }
  rft::command snapshot() override { return last_cmd; }
  rft::command last_cmd;
};