#include <librft/node_kind.h>

namespace rft {
std::string to_string(const rft::NODE_KIND s) {
  switch (s) {
  case rft::NODE_KIND::CANDIDATE:
    return "CANDIDATE";
  case rft::NODE_KIND::FOLLOWER:
    return "FOLLOWER";
  case rft::NODE_KIND::LEADER:
    return "LEADER";
  case rft::NODE_KIND::ELECTION:
    return "ELECTION";
  default:
    return "!!! UNKNOW !!!";
  }
}

} // namespace rft