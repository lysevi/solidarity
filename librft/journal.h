#pragma once

#include <librft/command.h>
#include <librft/exports.h>
#include <librft/types.h>
#include <map>
#include <memory>
#include <shared_mutex>
#include <utility>
#include <unordered_map>

namespace rft::logdb {
/// log sequence numbder;
using index_t = int64_t;
const term_t UNDEFINED_TERM = std::numeric_limits<term_t>::min();
const index_t UNDEFINED_INDEX = {-1};

enum class LOG_ENTRY_KIND : uint8_t { APPEND, SNAPSHOT };

struct log_entry {
  log_entry()
      : term(UNDEFINED_TERM)
      , cmd()
      , kind(LOG_ENTRY_KIND::APPEND) {}

  term_t term;
  command cmd;
  LOG_ENTRY_KIND kind;
};

struct reccord_info {
  reccord_info(const log_entry &e, index_t lsn_) {
    lsn = lsn_;
    term = e.term;
    kind = e.kind;
  }

  reccord_info() noexcept {
    term = UNDEFINED_TERM;
    lsn = UNDEFINED_INDEX;
    kind = LOG_ENTRY_KIND::APPEND;
  }

  bool is_empty() const { return lsn_is_empty() && term_is_empty(); }
  bool lsn_is_empty() const { return lsn == UNDEFINED_INDEX; }
  bool term_is_empty() const { return term == UNDEFINED_TERM; }

  bool operator==(const reccord_info &o) const { return term == o.term && lsn == o.lsn; }
  bool operator!=(const reccord_info &o) const { return !(*this == o); }

  term_t term;
  logdb::index_t lsn;
  LOG_ENTRY_KIND kind;
};

EXPORT std::string to_string(const reccord_info &ri);

class abstract_journal {
public:
  virtual ~abstract_journal() {}
  virtual reccord_info put(const log_entry &e) = 0;
  virtual void commit(const index_t lsn) = 0;
  virtual log_entry get(const logdb::index_t lsn) = 0;
  virtual size_t size() const = 0;
  
  virtual void erase_all_after(const index_t lsn) = 0;
  virtual void erase_all_to(const index_t lsn) = 0;

  virtual void visit(std::function<void(const log_entry &)>) = 0;

  virtual reccord_info prev_rec() const noexcept = 0;
  virtual reccord_info first_uncommited_rec() const noexcept = 0;
  virtual reccord_info commited_rec() const noexcept = 0;
  virtual reccord_info first_rec() const noexcept = 0;
  virtual reccord_info restore_start_point() const noexcept = 0;
  virtual reccord_info info(index_t lsn) const noexcept = 0;

  virtual std::unordered_map<index_t, log_entry> dump() const = 0;
};

using journal_ptr = std::shared_ptr<abstract_journal>;

class memory_journal final : public abstract_journal {
public:
  EXPORT static std::shared_ptr<memory_journal> make_new();
  EXPORT reccord_info put(const log_entry &e) override;
  EXPORT void commit(const index_t lsn) override;
  EXPORT log_entry get(const logdb::index_t lsn) override;
  EXPORT size_t size() const override;
  EXPORT void erase_all_after(const index_t lsn) override;
  EXPORT void erase_all_to(const index_t lsn) override;
  EXPORT void visit(std::function<void(const log_entry &)>) override;

  EXPORT reccord_info prev_rec() const noexcept override;
  EXPORT reccord_info first_uncommited_rec() const noexcept override;
  EXPORT reccord_info commited_rec() const noexcept override;
  EXPORT reccord_info first_rec() const noexcept override;
  EXPORT reccord_info restore_start_point() const noexcept override;
  EXPORT reccord_info info(index_t lsn) const noexcept override;

  EXPORT std::unordered_map<index_t, log_entry> dump() const override;

protected:
  mutable std::shared_mutex _locker;
  std::map<index_t, log_entry> _wal;

  index_t _idx = {};
  reccord_info _prev;
  reccord_info _commited;
};
} // namespace rft::logdb

namespace std {
template <>
struct hash<rft::logdb::reccord_info> {
  std::size_t operator()(const rft::logdb::reccord_info &k) const {
    size_t h1 = std::hash<rft::term_t>()(k.term);
    size_t h2 = std::hash<rft::logdb::index_t>()(k.lsn);
    return h1 ^ (h2 << 1);
  }
};
} // namespace std