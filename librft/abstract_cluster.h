#pragma once

#include <librft/journal.h>
#include <libutils/property.h>
#include <memory>

namespace rft {

class cluster_node {
  PROPERTY(std::string, name);

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

// leader term
using term_t = uint64_t;

enum class entries_kind_t { VOTE, APPEND, ANSWER };

struct append_entries {
  /// on election;
  entries_kind_t kind = {entries_kind_t::APPEND};
  /// round number;
  round_t round;
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
};
}; // namespace rft

namespace std {
template <>
struct hash<rft::cluster_node> {
  std::size_t operator()(const rft::cluster_node &k) const {
    return std::hash<string>()(k.name());
  }
};
} // namespace std