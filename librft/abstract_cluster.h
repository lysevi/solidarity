#pragma once

#include <librft/journal.h>
#include <libutils/utils.h>
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

enum class entries_kind_t { VOTE };

struct append_entries {

  bool is_vote;

  round_t round;
  uint64_t starttime; /// sender uptime
  cluster_node leader_term;

  logdb::reccord_info current;
  logdb::reccord_info prev;
  logdb::reccord_info commited;
};

class abstract_cluster {
public:
  virtual ~abstract_cluster() {}
  virtual void send_to(const cluster_node &from, const cluster_node &to,
                       const append_entries &m) = 0;
  virtual void send_all(const cluster_node &from, const append_entries &m) = 0;
  /// total nodes count
  virtual size_t size() = 0;
};
}; // namespace rft