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
  default:
    NOT_IMPLEMENTED;
  }
}
} // namespace rft