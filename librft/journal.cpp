#include <librft/journal.h>

using namespace rft;
using namespace logdb;

std::shared_ptr<memory_journal> memory_journal::make_new() {
  return std::make_shared<memory_journal>();
}

reccord_info memory_journal::put(const log_entry &e) {
  std::lock_guard<std::shared_mutex> lg(_locker);
  _wal.insert(std::make_pair(_idx, e));
  _prev.lsn = _idx;
  _prev.round = e.round;
  ++_idx;
  return _prev;
}

void memory_journal::commit(const reccord_info &i) {
  std::lock_guard<std::shared_mutex> lg(_locker);
  // TODO round check
  using mtype = std::map<index_t, log_entry>::const_iterator::value_type;
  std::vector<mtype> to_commit;
  to_commit.reserve(_wal.size());
  std::copy_if(_wal.cbegin(), _wal.cend(), std::back_inserter(to_commit),
               [i](auto kv) { return kv.first <= i.lsn; });

  for (auto kv : to_commit) {
    index_t idx = kv.first;
    log_entry e = kv.second;
    _commited_data.insert(std::make_pair(idx, e));
    _wal.erase(kv.first);
  }
  _commited = i;
}

log_entry memory_journal::get(const reccord_info &r) {
  std::shared_lock<std::shared_mutex> lg(_locker);
  // TODO check _prev and _commited for better speed;

  const auto wal_it = _wal.find(r.lsn);
  if (wal_it != _wal.end()) {
    return wal_it->second;
  }
  const auto commited_it = _commited_data.find(r.lsn);
  if (commited_it != _commited_data.end()) {
    return commited_it->second;
  }
  throw std::exception("data not founded");
}

size_t memory_journal::size() const {
  std::shared_lock<std::shared_mutex> lg(_locker);
  return _wal.size() + _commited_data.size();
}

reccord_info memory_journal::prev_rec() const {
  std::shared_lock<std::shared_mutex> lg(_locker);
  return _prev;
}
reccord_info memory_journal::commited_rec() const {
  std::shared_lock<std::shared_mutex> lg(_locker);
  return _commited;
}
