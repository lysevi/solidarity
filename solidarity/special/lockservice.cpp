#include <solidarity/special/lockservice.h>

#include <iostream>
#include <msgpack.hpp>

using namespace solidarity;
using namespace solidarity::special;

lockservice::lock_action lockservice::lock_action::from_cmd(const command &cmd) {
  lockservice::lock_action res;
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

command lockservice::lock_action::to_cmd() const {
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> pk(&buffer);
  pk.pack(target);
  pk.pack(owner);
  pk.pack(state);

  auto needed_size = buffer.size();
  command res(needed_size);
  memcpy(res.data.data(), buffer.data(), buffer.size());
  return res;
}

lockservice::lockservice() {}

void lockservice::apply_cmd(const command &cmd) {
  std::lock_guard l(_locker);
  auto a = lock_action::from_cmd(cmd);
  if (auto it = _locks.find(a.target); it != _locks.end()) {
    if (it->second.owner == a.owner || !it->second.state) {
      it->second = a;
    } else {
      return;
    }
  } else {
    _locks[a.target] = a;
  }
};

void lockservice::reset() {
  std::lock_guard l(_locker);
  lock_action empty;
  empty.owner = std::string();
  empty.state = false;
  for (auto &kv : _locks) {
    kv.second = empty;
  }
};

command lockservice::snapshot() {
  std::lock_guard l(_locker);
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> pk(&buffer);
  pk.pack(_locks.size());
  for (auto &kv : _locks) {
    pk.pack(kv.first);
    pk.pack(kv.second.owner);
    pk.pack(kv.second.state);
  }

  auto needed_size = buffer.size();
  command res(needed_size);
  memcpy(res.data.data(), buffer.data(), buffer.size());
  return res;
}

void lockservice::install_snapshot(const command &cmd) {
  std::lock_guard l(_locker);
  lockservice::lock_action res;
  msgpack::unpacker pac;
  pac.reserve_buffer(cmd.size());
  memcpy(pac.buffer(), cmd.data.data(), cmd.size());
  pac.buffer_consumed(cmd.size());

  msgpack::object_handle oh;
  pac.next(oh);
  auto sz = oh.get().as<size_t>();
  for (size_t i = 0; i < sz; ++i) {
    pac.next(oh);
    lock_action la;
    la.target = oh.get().as<std::string>();
    pac.next(oh);
    la.owner = oh.get().as<std::string>();
    pac.next(oh);
    la.state = oh.get().as<bool>();
    _locks[la.target] = la;
  }
}

command lockservice::read(const command &cmd) {
  std::lock_guard l(_locker);
  auto a = lock_action::from_cmd(cmd);
  if (auto it = _locks.find(a.target); it != _locks.end()) {
    lock_action la;
    la.target = a.target;
    la.owner = it->second.owner;
    la.state = it->second.state;
    return la.to_cmd();
  } else {
    return command(0);
  }
}

bool lockservice::can_apply(const command &cmd) {
  std::lock_guard l(_locker);
  auto a = lock_action::from_cmd(cmd);
  if (auto it = _locks.find(a.target); it != _locks.end()) {
    if (!it->second.state || it->second.owner == a.owner) {
      return true;
    }
    return false;
  }
  return true;
};

lockservice_client::lockservice_client(const std::string &owner,
                                       const std::shared_ptr<client> &c) {
  _owner = owner;
  _client = c;
}

bool lockservice_client::try_lock(const std::string &target) {
  if (!_client->is_connected()) {
    return false;
  }

  lockservice::lock_action la;
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

void lockservice_client::lock(const std::string &target) {
  while (true) {
    bool is_success = try_lock(target);
    if (is_success) {
      break;
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
  }
}

void lockservice_client::unlock(const std::string &target) {
  lockservice::lock_action la;
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