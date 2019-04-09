#include <librft/error_codes.h>
#include <libutils/utils.h>
namespace rft {
std::string to_string(const ERROR_CODE status) {
  switch (status) {
  case ERROR_CODE::OK: {
    return "OK";
  }
  case ERROR_CODE::NOT_A_LEADER: {
    return "NOT_A_LEADER";
  }
  case ERROR_CODE::CONNECTION_NOT_FOUND: {
    return "CONNECTION_NOT_FOUND";
  }
  case ERROR_CODE::WRONG_PROTOCOL_VERSION: {
    return "WRONG_PROTOCOL_VERSION";
  }

  case ERROR_CODE::UNDER_ELECTION: {
    return "UNDER_ELECTION";
  }

  case ERROR_CODE::UNDEFINED: {
    return "UNDER_ELECTION";
  }

  default:
    NOT_IMPLEMENTED;
  }
}
} // namespace rft