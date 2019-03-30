#include <librft/journal.h>
#include <libutils/exception.h>
#include <sstream>
using namespace rft;
using namespace logdb;

namespace rft {
namespace logdb {

std::string to_string(const reccord_info &ri) {
  std::stringstream ss;
  ss << "{t:" << ri.term << ", lsn:" << ri.lsn << "}";
  return ss.str();
}
} // namespace logdb
} // namespace rft

std::shared_ptr<memory_journal> memory_journal::make_new() {
  return std::make_shared<memory_journal>();
}

reccord_info memory_journal::put(const log_entry &e) {
  std::lock_guard<std::shared_mutex> lg(_locker);
  _wal.insert(std::make_pair(_idx, e));
  _prev = reccord_info(e, _idx);
  ++_idx;
  return _prev;
}

void memory_journal::commit(const index_t lsn) {
  std::lock_guard<std::shared_mutex> lg(_locker);

  auto to_commit = lsn;
  auto last = _wal.rbegin();

  if (last->first < lsn) {
    to_commit = last->first;
  }

  _commited = reccord_info(last->second, last->first);
}

log_entry memory_journal::get(const logdb::index_t lsn) {
  std::shared_lock<std::shared_mutex> lg(_locker);
  // TODO check _prev and _commited for better speed;

  if (const auto wal_it = _wal.find(lsn);wal_it != _wal.end()) {
    return wal_it->second;
  }

  THROW_EXCEPTION("memory_journal: data not founded");
}

size_t memory_journal::size() const {
  std::shared_lock<std::shared_mutex> lg(_locker);
  return _wal.size();
}

reccord_info memory_journal::prev_rec() const noexcept {
  std::shared_lock<std::shared_mutex> lg(_locker);
  return _prev;
}

reccord_info memory_journal::first_uncommited_rec() const noexcept {
  std::shared_lock<std::shared_mutex> lg(_locker);
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
  std::shared_lock<std::shared_mutex> lg(_locker);
  return _commited;
}

reccord_info memory_journal::first_rec() const noexcept {
  std::shared_lock<std::shared_mutex> lg(_locker);
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
    if (it->first == lsn /*&& it->second.term == e.term*/) {
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
  std::shared_lock<std::shared_mutex> lg(_locker);
  for (const auto &kv : _wal) {
    f(kv.second);
  }
}

reccord_info memory_journal::info(index_t lsn) const noexcept {
  std::shared_lock<std::shared_mutex> lg(_locker);
  if (auto it = _wal.find(lsn); it != _wal.end()) {
    return reccord_info(it->second, lsn);
    
  }
  return reccord_info{};
}

std::unordered_map<index_t, log_entry> memory_journal::dump() const {
  std::shared_lock<std::shared_mutex> lg(_locker);
  std::unordered_map<rft::logdb::index_t, rft::logdb::log_entry> result;

  auto prev = prev_rec();
  if (!prev.is_empty()) {
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