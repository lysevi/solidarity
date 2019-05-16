#pragma once

#include <solidarity/exports.h>

#include <cstdint>
#include <string>
#include <limits>


namespace solidarity {

enum class ERROR_CODE : uint16_t {
  OK = 0,
  NOT_A_LEADER,
  CONNECTION_NOT_FOUND,
  WRONG_PROTOCOL_VERSION,
  UNDER_ELECTION,
  NETWORK_ERROR,
  STATE_MACHINE_CAN_T_APPLY_CMD,
  LAST,
  UNDEFINED = std::numeric_limits<uint16_t>::max()
};
[[nodiscard]] EXPORT std::string to_string(const ERROR_CODE status);
} // namespace solidarity
