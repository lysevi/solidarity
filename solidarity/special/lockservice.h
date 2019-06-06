#pragma once

#include <solidarity/abstract_state_machine.h>
#include <solidarity/client.h>
#include <solidarity/exports.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace solidarity::special {
class lockservice : public abstract_state_machine {
public:
  struct lock_action {
    bool state = false;
    std::string target;
    std::string owner;

    EXPORT static lock_action from_cmd(const command_t &cmd);
    EXPORT command_t to_cmd() const;
  };
  EXPORT lockservice();

  EXPORT void apply_cmd(const command_t &cmd) override;

  EXPORT void reset() override;
  EXPORT command_t snapshot() override;
  EXPORT void install_snapshot(const command_t &cmd) override;

  EXPORT command_t read(const command_t &cmd) override;

  EXPORT bool can_apply(const command_t &) override;

  std::unordered_map<std::string, lock_action> get_locks() const { return _locks; }

private:
  std::unordered_map<std::string, lock_action> _locks;
  std::mutex _locker;
};

class lockservice_client {
public:
  EXPORT lockservice_client(const std::string &owner, const std::shared_ptr<client> &c);
  EXPORT bool try_lock(const std::string &target);
  EXPORT void lock(const std::string &target);
  EXPORT void unlock(const std::string &target);

private:
  std::shared_ptr<client> _client;
  std::string _owner;
};
} // namespace solidarity::special