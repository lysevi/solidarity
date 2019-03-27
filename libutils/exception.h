#pragma once
#include <libutils/logger.h>
#include <libutils/strings.h>
#include <stdexcept>
#include <string>

#define CODE_POS (utils::exceptions::codepos(__FILE__, __LINE__, __FUNCTION__))

#define MAKE_EXCEPTION(msg) utils::exceptions::exception_t::create_and_log(CODE_POS, msg)
// macros, because need CODE_POS

#ifdef DEBUG
#define THROW_EXCEPTION(...)                                                             \
  throw utils::exceptions::exception_t::create_and_log(CODE_POS,                         \
                                                       __VA_ARGS__) // std::exit(1);
#else
#define THROW_EXCEPTION(...)                                                             \
  throw utils::exceptions::exception_t::create_and_log(CODE_POS, __VA_ARGS__)
#endif

namespace utils::exceptions {

struct codepos {
  const char *_file;
  const int _line;
  const char *_func;

  codepos(const char *file, int line, const char *function)
      : _file(file)
      , _line(line)
      , _func(function) {}

  std::string toString() const {
    auto ss = std::string(_file) + " line: " + std::to_string(_line)
              + " function: " + std::string(_func) + "\n";
    return ss;
  }
  codepos &operator=(const codepos &) = delete;
};

class exception_t final : public std::exception {
public:
  template <typename... T>
  static exception_t create_and_log(const codepos &pos, T... message) {

    auto expanded_message = utils::strings::args_to_string(
        std::string("FATAL ERROR. The Exception will be thrown! "),
        pos.toString() + " Message: ",
        message...);
    return exception_t(expanded_message);
  }

public:
  virtual const char *what() const noexcept { return _msg.c_str(); }
  const std::string &message() const { return _msg; }

  exception_t();
  exception_t(const char *&message);
  exception_t(const std::string &message);

protected:
  void init_msg(const std::string &msg_);

private:
  std::string _msg;
};
} // namespace utils::exceptions
