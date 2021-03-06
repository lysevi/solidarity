#pragma once

namespace solidarity::utils {
/// Generate get_PROPERTY, set_PROPERTY, TYPE _PROPERTY
#define PROPERTY(TYPE, NAME)                                                             \
                                                                                         \
protected:                                                                               \
  TYPE _##NAME;                                                                          \
                                                                                         \
public:                                                                                  \
  [[nodiscard]] TYPE NAME() const { return _##NAME; }                                    \
  auto set_##NAME(TYPE __##NAME)->decltype(*this) {                                      \
    _##NAME = __##NAME;                                                                  \
    return *this;                                                                        \
  }
} // namespace solidarity::utils
