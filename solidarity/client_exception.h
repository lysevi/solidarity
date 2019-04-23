#pragma once

#include <exception>
#include <string>

namespace solidarity {
class exception : public std::exception {
public:
  exception(std::string m)
      : _message(m) {}
  [[nodiscard]] const char *what() const noexcept override { return _message.c_str(); }

private:
  std::string _message;
};

} // namespace solidarity
