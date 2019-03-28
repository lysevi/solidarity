#include <librft/abstract_cluster.h>
#include <libutils/strings.h>
#include <libutils/utils.h>

namespace std {
std::string to_string(const rft::ENTRIES_KIND k) {
  switch (k) {
  case rft::ENTRIES_KIND::HEARTBEAT:
    return "HEARTBEAT";
  case rft::ENTRIES_KIND::VOTE:
    return "VOTE";
  case rft::ENTRIES_KIND::APPEND:
    return "APPEND";
  case rft::ENTRIES_KIND::ANSWER_OK:
    return "ANSWER_OK";
  case rft::ENTRIES_KIND::ANSWER_FAILED:
    return "ANSWER_FAILED";
  case rft::ENTRIES_KIND::HELLO:
    return "HELLO";
  }
  NOT_IMPLEMENTED
}

std::string to_string(const rft::append_entries &e) {
  return utils::strings::args_to_string(
      "{cmd:", e.cmd.data.size(), " kind:", e.kind, " L:", e.leader, " term:", e.term,
      " cur:", e.current, " prev:", e.prev, " ci:", e.commited, "}");
}
} // namespace std