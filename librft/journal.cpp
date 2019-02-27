#include <librft/journal.h>

using namespace rft;
using namespace logdb;

std::shared_ptr<memory_journal> memory_journal::make_new() {
  return std::make_shared<memory_journal>();
}

void memory_journal::put(const log_entry &e) {
  _wal.insert(std::make_pair(_idx, e));
  _prev.lsn = _idx;
  _prev.round = e.round;
  ++_idx;
}

void memory_journal::commit(const reccord_info &i) {
  // TODO round check
  std::vector<typename std::map<index_t, log_entry>::const_iterator::value_type>
      to_commit;
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

size_t memory_journal::size() const {
  return _wal.size() + _commited_data.size();
}

reccord_info memory_journal::prev_rec() const {
  return _prev;
}
reccord_info memory_journal::commited_rec() const {
  return _commited;
}
