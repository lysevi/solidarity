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
const round_t UNDEFINED_ROUND = std::numeric_limits<round_t>::min();
const index_t UNDEFINED_INDEX = std::numeric_limits<logdb::index_t>::max();

struct reccord_info {
  reccord_info() noexcept {
    round = UNDEFINED_ROUND;
    lsn = UNDEFINED_INDEX;
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
  command cmd;
};

class abstract_journal {
public:
  ~abstract_journal() {}
  virtual void put(const log_entry &e) = 0;
  virtual void commit(const reccord_info &i) = 0;
  virtual log_entry get(const reccord_info &r) = 0;
  virtual size_t size() const = 0;

  virtual reccord_info prev_rec() const = 0;
  virtual reccord_info commited_rec() const = 0;
};

using journal_ptr = std::shared_ptr<abstract_journal>;

// TODO thread saffety
class memory_journal : public abstract_journal {
public:
  EXPORT static std::shared_ptr<memory_journal> make_new();
  EXPORT void put(const log_entry &e) override;
  EXPORT void commit(const reccord_info &i) override;
  EXPORT log_entry get(const reccord_info &r) override;
  EXPORT size_t size() const override;

  EXPORT reccord_info prev_rec() const override;
  EXPORT reccord_info commited_rec() const override;

protected:
  std::map<index_t, log_entry> _wal;
  std::map<index_t, log_entry> _commited_data;

  index_t _idx = {};
  reccord_info _prev;
  reccord_info _commited;
};
} // namespace logdb
} // namespace rft