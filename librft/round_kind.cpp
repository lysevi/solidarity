#include <librft/round_kind.h>

namespace rft {
std::string to_string(const rft::ROUND_KIND s) {
  switch (s) {
  case rft::ROUND_KIND::CANDIDATE:
    return "CANDIDATE";
  case rft::ROUND_KIND::FOLLOWER:
    return "FOLLOWER";
  case rft::ROUND_KIND::LEADER:
    return "LEADER";
  case rft::ROUND_KIND::ELECTION:
    return "ELECTION";
  default:
    return "!!! UNKNOW !!!";
  }
}

} // namespace rft