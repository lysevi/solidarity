#include <librft/settings.h>
#include <libutils/logger.h>

using namespace rft;

void node_settings::dump_to_log(utils::logging::abstract_logger *const l) {
  l->info("name: ", _name);
  l->info("election_timeout(ms): ", _election_timeout.count());
  l->info("vote_quorum(%): ", _vote_quorum);
  l->info("append_quorum(%): ", _append_quorum);
  l->info("cycle_for_replication: ", _cycle_for_replication);
  l->info("max_log_size: ", _max_log_size);
}