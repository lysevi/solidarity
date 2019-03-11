#include <librft/journal.h>

using namespace rft;
using namespace logdb;

namespace rft {
namespace logdb {
namespace inner {
using cmtype = std::map<index_t, log_entry>::const_iterator::value_type;
} // namespace inner

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
  // TODO term check
  using inner::cmtype;

  std::vector<cmtype> to_commit;
  to_commit.reserve(_wal.size());

  std::copy_if(_wal.cbegin(), _wal.cend(), std::back_inserter(to_commit),
               [lsn](const cmtype &kv) -> bool { return kv.first <= lsn; });

  /*std::sort(to_commit.begin(), to_commit.end(),
            [](const mtype &l, const mtype &r) -> bool { return l.first < r.first; });*/

  for (auto &&kv : to_commit) {
    index_t idx = kv.first;
    log_entry e = kv.second;
    _commited_data.insert(std::make_pair(idx, e));
    _wal.erase(kv.first);
  }
  auto last = to_commit.back();
  _commited.lsn = last.first;
  _commited.term = last.second.term;
}

log_entry memory_journal::get(const logdb::index_t lsn) {
  std::shared_lock<std::shared_mutex> lg(_locker);
  // TODO check _prev and _commited for better speed;

  const auto wal_it = _wal.find(lsn);
  if (wal_it != _wal.end()) {
    return wal_it->second;
  }
  const auto commited_it = _commited_data.find(lsn);
  if (commited_it != _commited_data.end()) {
    return commited_it->second;
  }
  throw std::exception("data not founded");
}

size_t memory_journal::size() const {
  std::shared_lock<std::shared_mutex> lg(_locker);
  return _wal.size() + _commited_data.size();
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
    auto front = _wal.begin();

    result.lsn = front->first;
    result.term = front->second.term;
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

  if (!_commited_data.empty()) {
    auto f = _commited_data.cbegin();
    if (result.lsn > f->first) {
      result.lsn = f->first;
      result.term = f->second.term;
    }
  }
  return result;
}

void memory_journal::erase_all_after(const reccord_info &e) {
  std::lock_guard<std::shared_mutex> lg(_locker);
  using inner::cmtype;

  auto erase_pred = [e](const cmtype &kv) -> bool { return kv.first > e.lsn; };
  std::vector<cmtype> to_commit;
  to_commit.reserve(std::max(_wal.size(), _commited_data.size()));

  std::copy_if(_wal.cbegin(), _wal.cend(), std::back_inserter(to_commit), erase_pred);
  for (auto &kv : to_commit) {
    _wal.erase(kv.first);
  }
  to_commit.clear();
  std::copy_if(_commited_data.cbegin(), _commited_data.cend(),
               std::back_inserter(to_commit), erase_pred);
  for (auto &kv : to_commit) {
    _commited_data.erase(kv.first);
  }

  if (!_wal.empty()) {
    auto it = _wal.rbegin();
    _prev.lsn = it->first;
    _prev.term = it->second.term;
  } else {
    _prev = reccord_info{};
  }

  if (!_commited_data.empty()) {
    auto it = _commited_data.rbegin();
    _commited.lsn = it->first;
    _commited.term = it->second.term;
  } else {
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

  for (const auto &kv : _commited_data) {
    f(kv.second);
  }
}