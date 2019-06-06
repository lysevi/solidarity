#pragma once

#include <solidarity/append_entries.h>
#include <solidarity/error_codes.h>
#include <solidarity/types.h>

namespace solidarity {

enum class RDIRECTION { FORWARDS = 0, BACKWARDS };
struct log_state_t {
  logdb::reccord_info prev;
  RDIRECTION direction = RDIRECTION::FORWARDS;
};

struct abstract_cluster_client {
  virtual ~abstract_cluster_client() {}
  virtual void recv(const node_name &from, const append_entries &e) = 0;
  virtual void lost_connection_with(const node_name &addr) = 0;
  virtual void new_connection_with(const node_name &addr) = 0;
  virtual void heartbeat() = 0;
  virtual ERROR_CODE add_command(const command_t &cmd) = 0;
  virtual std::unordered_map<node_name, log_state_t> journal_state() const = 0;
  virtual std::string leader() const = 0;
};

class abstract_cluster {
public:
  virtual ~abstract_cluster() {}
  virtual void
  send_to(const node_name &from, const node_name &to, const append_entries &m)
      = 0;
  virtual void send_all(const node_name &from, const append_entries &m) = 0;
  /// total nodes count
  virtual size_t size() = 0;
  virtual std::vector<node_name> all_nodes() const = 0;
};
} // namespace solidarity
