#pragma once

#include <librft/command.h>
#include <librft/exports.h>
#include <librft/types.h>
#include <map>
#include <memory>

namespace rft {

namespace logdb {
/// log sequence numbder;
using index_t = uint64_t;

struct reccord_info {
  reccord_info() {
    round = std::numeric_limits<round_t>::min();
    lsn = std::numeric_limits<logdb::index_t>::max();
  }

  bool is_empty() const {
    return round == std::numeric_limits<round_t>::min() &&
           lsn == std::numeric_limits<logdb::index_t>::max();
  }

  round_t round;
  logdb::index_t lsn;
};

struct log_entry {
  round_t round;
  command_ptr cmd;
};

class abstract_journal {
public:
  ~abstract_journal() {}
  virtual void put(index_t idx, const log_entry &e) = 0;
  virtual size_t size() const = 0;
};

using journal_ptr = std::shared_ptr<abstract_journal>;

class memory_journal : public abstract_journal {
public:
  static std::shared_ptr<memory_journal> make_new() {
    return std::make_shared<memory_journal>();
  }

  void put(index_t idx, const log_entry &e) override {
    _map.insert(std::make_pair(idx, e));
  }

  size_t size() const override { return _map.size(); }

protected:
  std::map<index_t, log_entry> _map;
};
} // namespace logdb
} // namespace rft