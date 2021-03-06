#include <solidarity/raft_settings.h>
#include <solidarity/utils/logger.h>

using namespace solidarity;

void raft_settings_t::dump_to_log(utils::logging::abstract_logger *const l) {
  l->info("name: ", _name);
  l->info("election_timeout(ms): ", _election_timeout.count());
  l->info("vote_quorum(%): ", _vote_quorum);
  l->info("append_quorum(%): ", _append_quorum);
  l->info("max_log_size: ", _max_log_size);
}