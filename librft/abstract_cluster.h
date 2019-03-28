#pragma once

#include <librft/journal.h>
#include <libutils/property.h>
#include <memory>

namespace rft {

class cluster_node {
  PROPERTY(std::string, name)

public:
  bool is_empty() const { return name().empty(); }
  void clear() { _name = ""; }
  bool operator!=(const cluster_node &other) const { return _name != other._name; }
  bool operator==(const cluster_node &other) const { return _name == other._name; }
  bool operator<(const cluster_node &other) const { return _name < other._name; }
};

inline std::string to_string(const cluster_node &s) {
  return std::string("node:://") + s.name();
}

enum class ENTRIES_KIND { HEARTBEAT, VOTE, APPEND, ANSWER_OK, ANSWER_FAILED, HELLO };

struct append_entries {
  /// on election;
  ENTRIES_KIND kind = {ENTRIES_KIND::APPEND};
  /// term number;
  term_t term;
  /// sender start time;
  uint64_t starttime = {};
  /// leader;
  cluster_node leader;
  /// cmd
  command cmd;
  logdb::reccord_info current;
  logdb::reccord_info prev;
  logdb::reccord_info commited;
};

class abstract_cluster {
public:
  virtual ~abstract_cluster() {}
  virtual void
  send_to(const cluster_node &from, const cluster_node &to, const append_entries &m)
      = 0;
  virtual void send_all(const cluster_node &from, const append_entries &m) = 0;
  /// total nodes count
  virtual size_t size() = 0;
  virtual std::vector<cluster_node> all_nodes() const = 0;
};
}; // namespace rft

namespace std {
template <>
struct hash<rft::cluster_node> {
  std::size_t operator()(const rft::cluster_node &k) const {
    return std::hash<string>()(k.name());
  }
};

EXPORT std::string to_string(const rft::append_entries &e);
EXPORT std::string to_string(const rft::ENTRIES_KIND k);
} // namespace std