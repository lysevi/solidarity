#pragma once

#include <librft/exports.h>
#include <cstdint>
#include <string>

namespace rft {
// TODO rename to error code
enum class ERROR_CODE : uint16_t {
  OK = 0,
  NOT_A_LEADER,
  CONNECTION_NOT_FOUND,
  WRONG_PROTOCOL_VERSION,
  UNDER_ELECTION,

  UNDEFINED = std::numeric_limits<uint16_t>::max()
};
EXPORT std::string to_string(const ERROR_CODE status);
} // namespace rft