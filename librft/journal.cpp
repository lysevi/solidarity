#include <librft/journal.h>

using namespace rft;
using namespace logdb;

namespace rft {
namespace logdb {

std::string to_string(const reccord_info &ri) {
  std::stringstream ss;
  ss << "{r:" << ri.term << ", lsn:" << ri.lsn << "}";
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
  _prev.lsn = _idx;
  _prev.term = e.term;
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

  _commited.lsn = last->first;
  _commited.term = last->second.term;
}

log_entry memory_journal::get(const logdb::index_t lsn) {
  std::shared_lock<std::shared_mutex> lg(_locker);
  // TODO check _prev and _commited for better speed;

  const auto wal_it = _wal.find(lsn);
  if (wal_it != _wal.end()) {
    return wal_it->second;
  }

  throw std::exception("data not founded");
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

void memory_journal::erase_all_after(const reccord_info &e) {
  std::lock_guard<std::shared_mutex> lg(_locker);

  using rmtype = std::map<index_t, log_entry>::reverse_iterator::value_type;
  std::vector<rmtype> to_erase;
  to_erase.reserve(_wal.size());
  for (auto it = _wal.rbegin(); it != _wal.rend(); ++it) {
    if (it->first == e.lsn && it->second.term == e.term) {
      break;
    }
    to_erase.push_back(*it);
  }
  for (auto &kv : to_erase) {
    _wal.erase(kv.first);
  }

  if (!_wal.empty()) {
    auto it = _wal.rbegin();
    _prev.lsn = it->first;
    _prev.term = it->second.term;

    if (_commited.lsn > e.lsn) {
      _commited = _prev;
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