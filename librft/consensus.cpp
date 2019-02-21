#include <librft/consensus.h>
#include <librft/utils/logger.h>

using namespace rft;
using namespace rft::utils::logging;

consensus::consensus(const node_settings &ns, const cluster_ptr &cluster,
                     const logdb::journal_ptr &jrn)
    : _settings(ns), _cluster(cluster), _jrn(jrn) {
  logger_info("node: ", ns.name(),
              " election_timeout(ms):", ns.election_timeout().count());

  heartbeat_time = clock_t::now();
}	

void consensus::on_timer() {
  auto now = clock_t::now();
  if ((now - heartbeat_time) > _settings.election_timeout()) {
    if (_cluster->size() == size_t(1)) {
      _round++;
      _state = CONSENSUS_STATE::LEADER;
    }
  }
}