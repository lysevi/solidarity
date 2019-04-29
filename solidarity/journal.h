#pragma once

#include <solidarity/command.h>
#include <solidarity/exports.h>
#include <solidarity/types.h>

#include <functional>
#include <map>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace solidarity::logdb {

enum class LOG_ENTRY_KIND : uint8_t { APPEND, SNAPSHOT };

struct log_entry {
  log_entry()
      : term(UNDEFINED_TERM)
      , cmd()
      , kind(LOG_ENTRY_KIND::APPEND) {}

  index_t idx = UNDEFINED_INDEX;
  term_t term;
  command cmd;
  uint32_t cmd_crc = 0;
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

  [[nodiscard]] bool is_empty() const { return lsn_is_empty() && term_is_empty(); }
  [[nodiscard]] bool lsn_is_empty() const { return lsn == UNDEFINED_INDEX; }
  [[nodiscard]] bool term_is_empty() const { return term == UNDEFINED_TERM; }

  [[nodiscard]] bool operator==(const reccord_info &o) const {
    return term == o.term && lsn == o.lsn;
  }
  [[nodiscard]] bool operator!=(const reccord_info &o) const { return !(*this == o); }

  term_t term;
  index_t lsn;
  LOG_ENTRY_KIND kind;
};

EXPORT std::string to_string(const reccord_info &ri);

class abstract_journal {
public:
  virtual ~abstract_journal() {}
  virtual reccord_info put(const index_t idx, const log_entry &e) = 0;
  virtual reccord_info put(const log_entry &e) = 0;
  virtual void commit(const index_t lsn) = 0;
  [[nodiscard]] virtual log_entry get(const index_t lsn) = 0;
  [[nodiscard]] virtual size_t size() const = 0;
  [[nodiscard]] virtual size_t reccords_count() const = 0;
  virtual void erase_all_after(const index_t lsn) = 0;
  virtual void erase_all_to(const index_t lsn) = 0;

  virtual void visit(std::function<void(const log_entry &)>) = 0;
  virtual void visit_after(const index_t lsn, std::function<void(const log_entry &)>) = 0;
  virtual void visit_to(const index_t lsn, std::function<void(const log_entry &)>) = 0;
  [[nodiscard]] virtual reccord_info prev_rec() const noexcept = 0;
  [[nodiscard]] virtual reccord_info first_uncommited_rec() const noexcept = 0;
  [[nodiscard]] virtual reccord_info commited_rec() const noexcept = 0;
  [[nodiscard]] virtual reccord_info first_rec() const noexcept = 0;
  [[nodiscard]] virtual reccord_info restore_start_point() const noexcept = 0;
  [[nodiscard]] virtual reccord_info info(index_t lsn) const noexcept = 0;

  virtual std::unordered_map<index_t, log_entry> dump() const = 0;
};

using journal_ptr = std::shared_ptr<abstract_journal>;

class memory_journal final : public abstract_journal {
public:
  [[nodiscard]] EXPORT static std::shared_ptr<memory_journal> make_new();

  EXPORT reccord_info put(const index_t idx, const log_entry &e) override;
  EXPORT reccord_info put(const log_entry &e) override;
  EXPORT void commit(const index_t lsn) override;
  [[nodiscard]] EXPORT log_entry get(const index_t lsn) override;
  [[nodiscard]] EXPORT size_t size() const override;
  [[nodiscard]] EXPORT size_t reccords_count() const override;
  EXPORT void erase_all_after(const index_t lsn) override;
  EXPORT void erase_all_to(const index_t lsn) override;
  EXPORT void visit(std::function<void(const log_entry &)>) override;
  EXPORT void visit_after(const index_t lsn,
                          std::function<void(const log_entry &)>) override;
  EXPORT void visit_to(const index_t lsn,
                       std::function<void(const log_entry &)>) override;

  [[nodiscard]] EXPORT reccord_info prev_rec() const noexcept override;
  [[nodiscard]] EXPORT reccord_info first_uncommited_rec() const noexcept override;
  [[nodiscard]] EXPORT reccord_info commited_rec() const noexcept override;
  [[nodiscard]] EXPORT reccord_info first_rec() const noexcept override;
  [[nodiscard]] EXPORT reccord_info restore_start_point() const noexcept override;
  [[nodiscard]] EXPORT reccord_info info(index_t lsn) const noexcept override;

  [[nodiscard]] EXPORT std::unordered_map<index_t, log_entry> dump() const override;

protected:
  mutable std::shared_mutex _locker;
  std::map<index_t, log_entry> _wal;

  index_t _idx = {};
  reccord_info _prev;
  reccord_info _commited;
};
} // namespace solidarity::logdb

namespace std {
template <>
struct hash<solidarity::logdb::reccord_info> {
  std::size_t operator()(const solidarity::logdb::reccord_info &k) const {
    size_t h1 = std::hash<solidarity::term_t>()(k.term);
    size_t h2 = std::hash<solidarity::index_t>()(k.lsn);
    return h1 ^ (h2 << 1);
  }
};
} // namespace std