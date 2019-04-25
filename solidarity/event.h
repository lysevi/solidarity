#pragma once

#include <solidarity/error_codes.h>
#include <solidarity/exports.h>
#include <solidarity/node_kind.h>

#include <optional>

namespace solidarity {

struct state_machine_updated_event_t {
  enum class event_kind : uint8_t {
    WAS_APPLIED,
    APPLY_ERROR,
    CAN_BE_APPLY,
    CAN_NOT_BE_APPLY,
    IN_LEADER_JOURNAL,
	ERASED_FROM_JOURNAL
  };

  event_kind kind;
  uint32_t crc;
};

struct network_state_event_t {
  ERROR_CODE ecode = ERROR_CODE::OK;
};

struct raft_state_event_t {
  NODE_KIND old_state;
  NODE_KIND new_state;
};

struct client_event_t {
  enum class event_kind { RAFT, NETWORK, STATE_MACHINE };

  event_kind kind;
  std::optional<raft_state_event_t> raft_ev;
  std::optional<network_state_event_t> net_ev;
  std::optional<state_machine_updated_event_t> state_ev;
};

[[nodiscard]] EXPORT std::string to_string(const client_event_t &cev);
} // namespace solidarity