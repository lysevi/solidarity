#pragma once

#include <memory>
#include <solidarity/error_codes.h>
#include <solidarity/journal.h>
#include <solidarity/utils/property.h>

namespace solidarity {

class node_name {
  PROPERTY(std::string, name)

public:
  node_name() = default;
  node_name(const node_name &) = default;
  node_name(node_name &&) = default;
  node_name(const std::string_view &sv)
      : _name(sv) {}

  node_name &operator=(const node_name &) = default;
  [[nodiscard]] bool is_empty() const { return name().empty(); }
  void clear() { _name = ""; }
  [[nodiscard]] bool operator!=(const node_name &other) const {
    return _name != other._name;
  }
  [[nodiscard]] bool operator==(const node_name &other) const {
    return _name == other._name;
  }
  [[nodiscard]] bool operator<(const node_name &other) const {
    return _name < other._name;
  }
};

[[nodiscard]] inline std::string to_string(const node_name &s) {
  return std::string("node:://") + s.name();
}

enum class ENTRIES_KIND : uint8_t {
  HEARTBEAT = 0,
  VOTE,
  APPEND,
  ANSWER_OK,
  ANSWER_FAILED,
  HELLO
};

struct append_entries {
  /// on election;
  ENTRIES_KIND kind = {ENTRIES_KIND::APPEND};
  /// term number;
  term_t term = UNDEFINED_TERM;
  /// sender start time;
  uint64_t starttime = {};
  /// leader;
  node_name leader;
  /// cmd
  command cmd;

  logdb::reccord_info current;
  logdb::reccord_info prev;
  logdb::reccord_info commited;

  [[nodiscard]] EXPORT std::vector<uint8_t> to_byte_array() const;
  [[nodiscard]] EXPORT static append_entries
  from_byte_array(const std::vector<uint8_t> &bytes);
};

struct abstract_cluster_client {
  virtual ~abstract_cluster_client() {}
  virtual void recv(const node_name &from, const append_entries &e) = 0;
  virtual void lost_connection_with(const node_name &addr) = 0;
  virtual void new_connection_with(const node_name &addr) = 0;
  virtual void heartbeat() = 0;
  virtual ERROR_CODE add_command(const command &cmd) = 0;
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

}; // namespace solidarity

namespace std {
template <>
struct hash<solidarity::node_name> {
  std::size_t operator()(const solidarity::node_name &k) const {
    return std::hash<string>()(k.name());
  }
};

EXPORT std::string to_string(const solidarity::append_entries &e);
EXPORT std::string to_string(const solidarity::ENTRIES_KIND k);
} // namespace std