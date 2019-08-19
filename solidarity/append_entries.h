#pragma once

#include <solidarity/journal.h>
#include <solidarity/types.h>

namespace solidarity {

enum class ENTRIES_KIND : uint8_t {
  HEARTBEAT = 0,
  VOTE,
  APPEND,
  ANSWER_OK,
  ANSWER_FAILED,
  HELLO
};

struct append_entries {
  ENTRIES_KIND kind = {ENTRIES_KIND::APPEND};
  /// term number;
  term_t term = UNDEFINED_TERM;
  /// sender start time;
  uint64_t starttime = {};
  /// leader;
  node_name leader;
  /// cmd
  command_t cmd;

  logdb::reccord_info current;
  logdb::reccord_info prev;
  logdb::reccord_info commited;

  [[nodiscard]] EXPORT std::vector<uint8_t> to_byte_array() const;
  [[nodiscard]] EXPORT static append_entries
  from_byte_array(const std::vector<uint8_t> &bytes);
};
} // namespace solidarity
namespace std {
EXPORT std::string to_string(const solidarity::append_entries &e);
EXPORT std::string to_string(const solidarity::ENTRIES_KIND k);
} // namespace std