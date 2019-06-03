#pragma once

#include <solidarity/error_codes.h>
#include <solidarity/journal.h>

#include <memory>
#include <string_view>

namespace solidarity {

using node_name = std::string;

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
  command cmd;
  /// abstract state machine id
  uint32_t asm_num = 0;

  logdb::reccord_info current;
  logdb::reccord_info prev;
  logdb::reccord_info commited;

  [[nodiscard]] EXPORT std::vector<uint8_t> to_byte_array() const;
  [[nodiscard]] EXPORT static append_entries
  from_byte_array(const std::vector<uint8_t> &bytes);
};

enum class RDIRECTION { FORWARDS = 0, BACKWARDS };
struct log_state_t {
  logdb::reccord_info prev;
  RDIRECTION direction = RDIRECTION::FORWARDS;
};

struct abstract_cluster_client {
  virtual ~abstract_cluster_client() {}
  virtual void recv(const node_name &from, const append_entries &e) = 0;
  virtual void lost_connection_with(const node_name &addr) = 0;
  virtual void new_connection_with(const node_name &addr) = 0;
  virtual void heartbeat() = 0;
  virtual ERROR_CODE add_command(const command &cmd) = 0;
  virtual std::unordered_map<node_name, log_state_t> journal_state() const = 0;
  virtual std::string leader() const = 0;
};

class abstract_cluster {
public:
  virtual ~abstract_cluster() {}
  virtual void
  send_to(const node_name &from, const node_name &to, const append_entries &m)
      = 0;
  virtual void send_all(const node_name &from, const append_entries &m) = 0;
  /// total nodes count
  virtual size_t size() = 0;
  virtual std::vector<node_name> all_nodes() const = 0;
};
} // namespace solidarity
namespace std {
EXPORT std::string to_string(const solidarity::append_entries &e);
EXPORT std::string to_string(const solidarity::ENTRIES_KIND k);
} // namespace std