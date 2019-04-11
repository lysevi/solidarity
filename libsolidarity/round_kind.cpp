#include <libsolidarity/node_kind.h>
#include <libsolidarity/utils/utils.h>

namespace solidarity {
std::string to_string(const solidarity::NODE_KIND s) {
  switch (s) {
  case solidarity::NODE_KIND::CANDIDATE:
    return "CANDIDATE";
  case solidarity::NODE_KIND::FOLLOWER:
    return "FOLLOWER";
  case solidarity::NODE_KIND::LEADER:
    return "LEADER";
  case solidarity::NODE_KIND::ELECTION:
    return "ELECTION";
  }
  NOT_IMPLEMENTED
}

} // namespace solidarity
