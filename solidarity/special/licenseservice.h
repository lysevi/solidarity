#pragma once

#include <solidarity/abstract_state_machine.h>
#include <solidarity/client.h>
#include <solidarity/exports.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace solidarity::special {
class licenseservice : public abstract_state_machine {
public:
  struct lock_action {
    std::string target;
    std::string owner;
    bool state;

    EXPORT static lock_action from_cmd(const command &cmd);
    EXPORT command to_cmd() const;
  };
  struct lock_state {
    std::unordered_set<std::string> owners;
  };
  EXPORT licenseservice(const std::unordered_map<std::string, size_t> &_lics);

  EXPORT void apply_cmd(const command &cmd) override;

  EXPORT void reset() override;
  EXPORT command snapshot() override;
  EXPORT void install_snapshot(const command &cmd) override;

  EXPORT command read(const command &cmd) override;

  EXPORT bool can_apply(const command &) override;

private:
  std::unordered_map<std::string, lock_state> _locks;
  std::unordered_map<std::string, size_t> _lics;
  std::mutex _locker;
};

class licenseservice_client {
public:
  EXPORT licenseservice_client(const std::string &owner,
                               const std::shared_ptr<client> &c);
  EXPORT bool try_lock(const std::string &target);
  EXPORT void unlock(const std::string &target);
  EXPORT std::vector<std::string> lockers(const std::string &target);

private:
  std::shared_ptr<client> _client;
  std::string _owner;
};
} // namespace solidarity::special