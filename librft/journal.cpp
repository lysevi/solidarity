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
  // TODO term check
  using mtype = std::map<index_t, log_entry>::const_iterator::value_type;

  std::vector<mtype> to_commit;
  to_commit.reserve(_wal.size());

  std::copy_if(_wal.cbegin(), _wal.cend(), std::back_inserter(to_commit),
               [lsn](const mtype &kv) -> bool { return kv.first <= lsn; });

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