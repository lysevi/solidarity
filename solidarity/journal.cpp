#include <solidarity/journal.h>
#include <solidarity/utils/exception.h>
#include <solidarity/utils/utils.h>
#include <sstream>
using namespace solidarity;
using namespace logdb;

namespace solidarity {
namespace logdb {

log_entry::log_entry()
    : term(UNDEFINED_TERM)
    , cmd()
    , kind(LOG_ENTRY_KIND::APPEND) {}

log_entry::log_entry(const log_entry &o)
    : idx(o.idx)
    , term(o.term)
    , cmd(o.cmd)
    , cmd_crc(o.cmd_crc)
    , kind(o.kind) {}

log_entry::log_entry(log_entry &&o) noexcept
    : idx(o.idx)
    , term(o.term)
    , cmd(std::move(o.cmd))
    , cmd_crc(o.cmd_crc)
    , kind(o.kind) {}

std::string to_string(const reccord_info &ri) {
  std::stringstream ss;
  ss << "{t:" << ri.term << ", lsn:" << ri.lsn << "}";
  return ss.str();
}
} // namespace logdb
} // namespace solidarity

std::shared_ptr<memory_journal> memory_journal::make_new() {
  return std::make_shared<memory_journal>();
}

memory_journal::~memory_journal() {
  _wal.clear();
}

reccord_info memory_journal::put(const log_entry &e) {
  std::lock_guard<std::shared_mutex> lg(_locker);
  _wal.insert(std::make_pair(_idx, e));
  _prev = reccord_info(e, _idx);
  ++_idx;
  return _prev;
}

reccord_info memory_journal::put(const index_t idx, log_entry &e) {
  std::lock_guard<std::shared_mutex> lg(_locker);
  ENSURE(idx >= _idx);
  e.idx = idx;
  _idx = idx;
  _wal.insert(std::make_pair(idx, e));
  _prev = reccord_info(e, e.idx);
  _idx++;
  return _prev;
}

void memory_journal::commit(const index_t lsn) {
  std::lock_guard<std::shared_mutex> lg(_locker);

  auto to_commit = lsn;
  auto last = _wal.rbegin();

  if (last->first < lsn) {
    to_commit = last->first;
  }
  auto r = _wal.find(to_commit);
  _commited = reccord_info(r->second, lsn);
}

log_entry memory_journal::get(const index_t lsn) {
  std::shared_lock lg(_locker);
  // TODO check _prev and _commited for better speed;

  if (const auto wal_it = _wal.find(lsn); wal_it != _wal.end()) {
    return wal_it->second;
  }

  THROW_EXCEPTION("memory_journal: data not founded");
}

size_t memory_journal::size() const {
  std::shared_lock lg(_locker);
  if (_prev.is_empty()) {
    return size_t(0);
  } else {
    return _prev.lsn + 1;
  }
}

size_t memory_journal::reccords_count() const {
  std::shared_lock lg(_locker);
  return _wal.size();
}

reccord_info memory_journal::prev_rec() const noexcept {
  std::shared_lock lg(_locker);
  return _prev;
}

reccord_info memory_journal::first_uncommited_rec() const noexcept {
  std::shared_lock lg(_locker);
  reccord_info result;
  if (_wal.empty()) {
    result.lsn = UNDEFINED_INDEX;
    result.term = UNDEFINED_TERM;
  } else {

    auto front = _commited.is_empty() ? _wal.cbegin() : _wal.find(_commited.lsn + 1);
    if (front == _wal.cend()) {
      result.lsn = UNDEFINED_INDEX;
      result.term = UNDEFINED_TERM;
    } else {
      result.lsn = front->first;
      result.term = front->second.term;
    }
  }
  return result;
}

reccord_info memory_journal::commited_rec() const noexcept {
  std::shared_lock lg(_locker);
  return _commited;
}

reccord_info memory_journal::first_rec() const noexcept {
  std::shared_lock lg(_locker);
  reccord_info result{};
  if (!_wal.empty()) {
    auto f = _wal.cbegin();
    result.lsn = f->first;
    result.term = f->second.term;
  }

  return result;
}

reccord_info memory_journal::restore_start_point() const noexcept {
  for (auto it = _wal.crbegin(); it != _wal.crend(); it++) {
    if (it->second.kind == LOG_ENTRY_KIND::SNAPSHOT) {
      return reccord_info(it->second, it->first);
    }
  }
  return first_rec();
}

void memory_journal::erase_all_after(const index_t lsn) {
  std::lock_guard<std::shared_mutex> lg(_locker);

  using rmtype = std::map<index_t, log_entry>::reverse_iterator::value_type;
  std::vector<rmtype> to_erase;
  to_erase.reserve(_wal.size());
  for (auto it = _wal.rbegin(); it != _wal.rend(); ++it) {
    if (it->first == lsn) {
      break;
    }
    to_erase.push_back(*it);
  }
  for (auto &kv : to_erase) {
    _wal.erase(kv.first);
  }

  if (!_wal.empty()) {
    auto it = _wal.rbegin();
    _prev = reccord_info(it->second, it->first);
    _idx = it->first + 1;
    if (_commited.lsn > lsn) {
      _commited = _prev;
    }
  } else {
    _idx = 0;
    _prev = reccord_info{};
    _commited = reccord_info{};
  }

  if (_prev.is_empty() && !_commited.is_empty()) {
    _prev = _commited;
  }
}

void memory_journal::erase_all_to(const index_t lsn) {
  std::lock_guard<std::shared_mutex> lg(_locker);

  using rmtype = std::map<index_t, log_entry>::reverse_iterator::value_type;
  std::vector<rmtype> to_erase;
  to_erase.reserve(_wal.size());

  for (auto it = _wal.begin(); it != _wal.end(); ++it) {
    if (it->first >= lsn) {
      break;
    }
    to_erase.push_back(*it);
  }

  for (auto &kv : to_erase) {
    _wal.erase(kv.first);
  }

  if (!_wal.empty()) {
    auto it = _wal.begin();

    if (_commited.lsn < lsn) {
      _commited = reccord_info(it->second, it->first);
    }
  } else {
    _prev = reccord_info{};
    _commited = reccord_info{};
  }

  if (_prev.is_empty() && !_commited.is_empty()) {
    _prev = _commited;
  }
}

void memory_journal::visit(std::function<void(const log_entry &)> f) {
  std::shared_lock lg(_locker);
  for (const auto &kv : _wal) {
    f(kv.second);
  }
}

void memory_journal::visit_after(const index_t lsn,
                                 std::function<void(const log_entry &)> e) {
  std::shared_lock lg(_locker);
  for (auto it = _wal.rbegin(); it != _wal.rend(); ++it) {
    if (it->first == lsn) {
      break;
    }
    e(it->second);
  }
}

void memory_journal::visit_to(const index_t lsn,
                              std::function<void(const log_entry &)> e) {
  std::shared_lock lg(_locker);

  for (auto it = _wal.begin(); it != _wal.end(); ++it) {
    if (it->first >= lsn) {
      break;
    }
    e(it->second);
  }
}

reccord_info memory_journal::info(index_t lsn) const noexcept {
  std::shared_lock lg(_locker);
  if (auto it = _wal.find(lsn); it != _wal.end()) {
    return reccord_info(it->second, lsn);
  }
  return reccord_info{};
}

std::unordered_map<index_t, log_entry> memory_journal::dump() const {
  std::unordered_map<solidarity::index_t, solidarity::logdb::log_entry> result;

  auto prev = prev_rec();
  if (!prev.is_empty()) {
    std::shared_lock lg(_locker);
    result.reserve(prev.lsn);

    while (prev.lsn >= 0) {
      auto it = _wal.find(prev.lsn);
      if (it == _wal.cend()) {
        break;
      }
      result.insert(std::pair(prev.lsn, it->second));
      --prev.lsn;
    }
  }
  return result;
}