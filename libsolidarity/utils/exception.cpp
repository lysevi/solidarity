#include <libsolidarity/utils/exception.h>
#include <boost/stacktrace.hpp>
#include <sstream>

using namespace utils::exceptions;

exception_t::exception_t() {
  init_msg("");
}
exception_t::exception_t(const char *&message) {
  init_msg(message);
}
exception_t::exception_t(const std::string &message) {
  init_msg(message);
}

void exception_t::init_msg(const std::string &msg_) {
  std::stringstream ss;
  ss << msg_
     << " Stacktrace:" << boost::stacktrace::to_string(boost::stacktrace::stacktrace());
  _msg = std::move(ss.str());
}
