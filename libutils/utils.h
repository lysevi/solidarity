#pragma once

#include <libutils/exception.h>
#include <atomic>

#define NOT_IMPLEMENTED THROW_EXCEPTION("Not implemented");

#ifdef DOUBLE_CHECKS
#define ENSURE_MSG(A, E)                                                                 \
  if (!(A)) {                                                                            \
    THROW_EXCEPTION(E);                                                                  \
  }
#define ENSURE(A) ENSURE_MSG(A, #A)
#else
#define ENSURE_MSG(A, E)
#define ENSURE(A)
#endif

#define UNUSED(x) (void)(x)

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

namespace utils {

inline void sleep_mls(long long a) {
  std::this_thread::sleep_for(std::chrono::milliseconds(a));
}

class non_copy {
private:
  non_copy(const non_copy &) = delete;
  non_copy &operator=(const non_copy &) = delete;

protected:
  non_copy() = default;
};

struct elapsed_time {
  elapsed_time() noexcept { start_time = clock(); }

  double elapsed() noexcept { return double(clock() - start_time) / CLOCKS_PER_SEC; }
  clock_t start_time;
};

} // namespace utils
