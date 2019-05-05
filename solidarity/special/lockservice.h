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
    bool state=false;
    std::string target;
    std::string owner;

    EXPORT static lock_action from_cmd(const command &cmd);
    EXPORT command to_cmd() const;
  };
  EXPORT lockservice();

  EXPORT void apply_cmd(const command &cmd) override;

  EXPORT void reset() override;
  EXPORT command snapshot() override;
  EXPORT void install_snapshot(const command &cmd) override;

  EXPORT command read(const command &cmd) override;

  EXPORT bool can_apply(const command &) override;

private:
  std::unordered_map<std::string, lock_action> _locks;
  std::mutex _locker;
};

class lockservice_client {
public:
  EXPORT lockservice_client(const std::string&owner, const std::shared_ptr<client> &c);
  EXPORT bool try_lock(const std::string &target);
  EXPORT void lock(const std::string &target);
  EXPORT void unlock(const std::string &target);
private:
  std::shared_ptr<client> _client;
  std::string _owner;
};
} // namespace solidarity::special