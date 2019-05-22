#pragma once

#include <solidarity/error_codes.h>
#include <solidarity/exports.h>
#include <solidarity/node_kind.h>
#include <solidarity/raft.h>

#include <optional>

namespace solidarity {

enum class command_status : uint8_t {
  WAS_APPLIED,
  APPLY_ERROR,
  CAN_BE_APPLY,
  CAN_NOT_BE_APPLY,
  IN_LEADER_JOURNAL,
  ERASED_FROM_JOURNAL,
  LAST
};

EXPORT std::string to_string(const command_status s);

struct command_status_event_t {
  command_status status;
  uint32_t crc;
};

struct network_state_event_t {
  ERROR_CODE ecode = ERROR_CODE::OK;
};

struct raft_state_event_t {
  NODE_KIND old_state;
  NODE_KIND new_state;
};

struct cluster_state_event_t {
  std::string leader;
  std::unordered_map<std::string, log_state_t> state;
};

struct client_event_t {
  enum class event_kind { UNDEFINED, RAFT, NETWORK, COMMAND_STATUS, CLUSTER_STATUS, LAST };

  event_kind kind = event_kind::UNDEFINED;
  std::optional<raft_state_event_t> raft_ev;
  std::optional<network_state_event_t> net_ev;
  std::optional<command_status_event_t> cmd_ev;
  std::optional<cluster_state_event_t> cluster_ev;
};

[[nodiscard]] EXPORT std::string to_string(const client_event_t &cev);
} // namespace solidarity