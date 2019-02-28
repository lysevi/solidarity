#pragma once

namespace utils {
/// Generate get_PROPERTY, set_PROPERTY, TYPE _PROPERTY
#define PROPERTY(TYPE, NAME)                                                             \
  \
protected:                                                                               \
  TYPE _##NAME;                                                                          \
  \
public:                                                                                  \
  TYPE NAME() const { return _##NAME; }                                                  \
  auto set_##NAME(TYPE __##NAME)->decltype(*this) {                                      \
    _##NAME = __##NAME;                                                                  \
    return *this;                                                                        \
  }
}