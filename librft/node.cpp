#include <librft/node.h>
#include <librft/utils/logger.h>

using namespace rft;
using namespace rft::utils::logging;

node::node(const node_settings &ns) : _ns(ns) {
  logger_info("node: ", ns.name(), " period(ms):", ns.period().count());
}