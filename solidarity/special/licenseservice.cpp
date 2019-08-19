#include <solidarity/special/licenseservice.h>

#include <iostream>
#include <msgpack.hpp>

using namespace solidarity;
using namespace solidarity::special;

licenseservice::lock_action licenseservice::lock_action::from_cmd(const command_t &cmd) {
  licenseservice::lock_action res;
  msgpack::unpacker pac;
  pac.reserve_buffer(cmd.size());
  memcpy(pac.buffer(), cmd.data.data(), cmd.size());
  pac.buffer_consumed(cmd.size());

  msgpack::object_handle oh;
  pac.next(oh);
  res.target = oh.get().as<std::string>();
  pac.next(oh);
  res.owner = oh.get().as<std::string>();
  pac.next(oh);
  res.state = oh.get().as<bool>();
  return res;
}

command_t licenseservice::lock_action::to_cmd() const {
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> pk(&buffer);
  pk.pack(target);
  pk.pack(owner);
  pk.pack(state);

  auto needed_size = buffer.size();
  command_t res(needed_size);
  memcpy(res.data.data(), buffer.data(), buffer.size());
  return res;
}

licenseservice::licenseservice(const std::unordered_map<std::string, size_t> &lics)
    : _lics(lics) {
  reset();
}

void licenseservice::reset() {
  std::lock_guard l(_locker);
  for (const auto &kv : _lics) {
    _locks.insert(std::pair(kv.first, lock_state{}));
  }
};

command_t licenseservice::snapshot() {
  std::lock_guard l(_locker);
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> pk(&buffer);
  pk.pack(_locks.size());
  for (auto &kv : _locks) {
    pk.pack(kv.first);
    pk.pack(kv.second.owners.size());
    for (auto &o : kv.second.owners) {
      pk.pack(o);
    }
  }

  auto needed_size = buffer.size();
  command_t res(needed_size);
  memcpy(res.data.data(), buffer.data(), buffer.size());
  return res;
}

void licenseservice::install_snapshot(const command_t &cmd) {
  std::lock_guard l(_locker);
  licenseservice::lock_action res;
  msgpack::unpacker pac;
  pac.reserve_buffer(cmd.size());
  memcpy(pac.buffer(), cmd.data.data(), cmd.size());
  pac.buffer_consumed(cmd.size());

  msgpack::object_handle oh;
  pac.next(oh);
  auto sz = oh.get().as<size_t>();
  for (size_t i = 0; i < sz; ++i) {
    pac.next(oh);
    auto s = oh.get().as<std::string>();
    pac.next(oh);
    auto ls_sz = oh.get().as<size_t>();
    lock_state lstate;
    lstate.owners.reserve(ls_sz);
    for (size_t j = 0; j < ls_sz; ++j) {
      pac.next(oh);
      auto o = oh.get().as<std::string>();
      lstate.owners.insert(o);
    }

    _locks[s] = lstate;
  }
}

command_t licenseservice::read(const command_t &cmd) {
  std::lock_guard l(_locker);
  auto a = lock_action::from_cmd(cmd);
  if (auto it = _locks.find(a.target); it != _locks.end()) {
    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> pk(&buffer);
    pk.pack(it->second.owners.size());
    for (auto &o : it->second.owners) {
      pk.pack(o);
    }

    auto needed_size = buffer.size();
    command_t res(needed_size);
    memcpy(res.data.data(), buffer.data(), buffer.size());
    return res;
  } else {
    return command_t(0);
  }
}

void licenseservice::apply_cmd(const command_t &cmd) {
  std::lock_guard l(_locker);
  auto a = lock_action::from_cmd(cmd);
  if (auto lit = _lics.find(a.target); lit != _lics.end()) {
    if (auto it = _locks.find(a.target); it != _locks.end()) {
      if (a.state && it->second.owners.size() < lit->second) {
        it->second.owners.insert(a.owner);
      }
      if (!a.state) {
        it->second.owners.erase(a.owner);
      }
    }
  }
};

bool licenseservice::can_apply(const command_t &cmd) {
  auto a = lock_action::from_cmd(cmd);
  if (!a.state) {
    return true;
  }

  std::lock_guard l(_locker);

  if (auto lit = _lics.find(a.target); lit != _lics.end()) {
    if (auto it = _locks.find(a.target); it != _locks.end()) {
      if (it->second.owners.size() < lit->second) {
        return true;
      }
      return false;
    }
  }
  return false;
};

licenseservice_client::licenseservice_client(const std::string &owner,
                                             const std::shared_ptr<client> &c) {
  _owner = owner;
  _client = c;
}

bool licenseservice_client::try_lock(const std::string &target) {
  if (!_client->is_connected()) {
    return false;
  }

  licenseservice::lock_action la;
  la.owner = _owner;
  la.target = target;
  la.state = true;
  auto la_cmd = la.to_cmd();

  auto sst = _client->send_strong(la_cmd);

  if (sst.ecode != ERROR_CODE::OK) {
    return false;
  }

  return sst.status == command_status::WAS_APPLIED;
}

void licenseservice_client::unlock(const std::string &target) {
  licenseservice::lock_action la;
  la.owner = _owner;
  la.target = target;
  la.state = false;

  while (true) {
    if (!_client->is_connected()) {
      break;
    }

    auto ec = _client->send_weak(la.to_cmd());

    if (ec == ERROR_CODE::NETWORK_ERROR || ec == ERROR_CODE::UNDER_ELECTION) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      continue;
    } else {
      break;
    }
  }
}

std::vector<std::string> licenseservice_client::lockers(const std::string &target) {
  licenseservice::lock_action la;
  la.owner = _owner;
  la.target = target;
  la.state = false;
  if (!_client->is_connected()) {
    return std::vector<std::string>();
  }

  auto cmd = _client->read(la.to_cmd());

  msgpack::unpacker pac;
  pac.reserve_buffer(cmd.size());
  memcpy(pac.buffer(), cmd.data.data(), cmd.size());
  pac.buffer_consumed(cmd.size());

  msgpack::object_handle oh;
  pac.next(oh);
  size_t sz = oh.get().as<size_t>();
  std::vector<std::string> res(sz);
  for (size_t i = 0; i < sz; ++i) {
    pac.next(oh);
    res[i] = oh.get().as<std::string>();
  }
  return res;
}