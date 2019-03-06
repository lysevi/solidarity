#pragma once

#include <librft/command.h>
#include <librft/exports.h>
#include <librft/types.h>
#include <map>
#include <memory>
#include <shared_mutex>
#include <utility>

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

  bool is_empty() const { return round == UNDEFINED_ROUND && lsn == UNDEFINED_INDEX; }

  bool operator==(const reccord_info &o) const {
    return round == o.round && lsn == o.lsn;
  }

  bool operator!=(const reccord_info &o) const { return !(*this == o); }

  round_t round;
  logdb::index_t lsn;
};

EXPORT std::string to_string(const reccord_info &ri);

struct log_entry {
  round_t round;
  command cmd;
};

class abstract_journal {
public:
  ~abstract_journal() {}
  virtual reccord_info put(const log_entry &e) = 0;
  virtual void commit(const index_t lsn) = 0;
  virtual log_entry get(const logdb::index_t lsn) = 0;
  virtual size_t size() const = 0;

  virtual reccord_info prev_rec() const noexcept = 0;
  virtual reccord_info first_uncommited_rec() const noexcept = 0;
  virtual reccord_info commited_rec() const noexcept = 0;
  virtual reccord_info first_rec() const noexcept = 0;
};

using journal_ptr = std::shared_ptr<abstract_journal>;

class memory_journal final : public abstract_journal {
public:
  EXPORT static std::shared_ptr<memory_journal> make_new();
  EXPORT reccord_info put(const log_entry &e) override;
  EXPORT void commit(const index_t lsn) override;
  EXPORT log_entry get(const logdb::index_t lsn) override;
  EXPORT size_t size() const override;

  EXPORT reccord_info prev_rec() const noexcept override;
  EXPORT reccord_info first_uncommited_rec() const noexcept override;
  EXPORT reccord_info commited_rec() const noexcept override;
  EXPORT reccord_info first_rec() const noexcept override;

protected:
  mutable std::shared_mutex _locker;
  std::map<index_t, log_entry> _wal;
  std::map<index_t, log_entry> _commited_data;

  index_t _idx = {};
  reccord_info _prev;
  reccord_info _commited;
};
} // namespace logdb
} // namespace rft

namespace std {
template <>
struct hash<rft::logdb::reccord_info> {
  std::size_t operator()(const rft::logdb::reccord_info &k) const {
    size_t h1 = std::hash<rft::round_t>()(k.round);
    size_t h2 = std::hash<rft::logdb::index_t>()(k.lsn);
    return h1 ^ (h2 << 1);
  }
};
} // namespace std