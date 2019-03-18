#include <librft/abstract_cluster.h>
#include <libutils/strings.h>
#include <libutils/utils.h>

namespace std {
std::string to_string(const rft::entries_kind_t k) {
  switch (k) {
  case rft::entries_kind_t::HEARTBEAT:
    return "HEARTBEAT";
  case rft::entries_kind_t::VOTE:
    return "VOTE";
  case rft::entries_kind_t::APPEND:
    return "APPEND";
  case rft::entries_kind_t::ANSWER_OK:
    return "ANSWER_OK";
  case rft::entries_kind_t::ANSWER_FAILED:
    return "ANSWER_FAILED";
  case rft::entries_kind_t::HELLO:
    return "HELLO";
  default:
    NOT_IMPLEMENTED;
  }
}

std::string to_string(const rft::append_entries &e) {
  return utils::strings::args_to_string(
      "{cmd:", e.cmd.data.size(), " kind:", e.kind, " L:", e.leader, " term:", e.term,
      " cur:", e.current, " prev:", e.prev, " ci:", e.commited, "}");
}
} // namespace std