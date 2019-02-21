#pragma once

#include <librft/journal.h>
#include <librft/utils/utils.h>
#include <memory>

namespace rft {

class cluster_node {
  PROPERTY(std::string, name);

  bool operator==(const cluster_node &other) const { return _name == other._name; }
  bool operator<(const cluster_node &other) const { return _name < other._name; }
};

// leader term
using term_t = uint64_t;

enum class entries_kind_t { VOTE };

struct append_entries {
  round_t round;
  logdb::reccord_info current;
  logdb::reccord_info prev;
  logdb::reccord_info commited;
};

class abstract_cluster {
public:
  virtual void send_to(cluster_node &from, cluster_node &to, const append_entries &m) = 0;
  /// total nodes count
  virtual size_t size() = 0;
};
using cluster_ptr = std::shared_ptr<abstract_cluster>;

}; // namespace rft