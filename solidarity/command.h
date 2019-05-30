#pragma once

#include <solidarity/exports.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace solidarity {

struct command {
  command() = default;
  command(const command &) = default;
  command(command &&) = default;
  command(const std::initializer_list<std::uint8_t> &il)
      : data(il) {}
  command(const std::vector<std::uint8_t> &o)
      : data(o) {}
  command(const size_t sz)
      : data(sz) {}
  command &operator=(const command &) = default;

  std::vector<std::uint8_t> data;

  bool is_empty() const { return data.empty(); }
  size_t size() const { return data.size(); }
  void resize(const size_t s) { return data.resize(s); }

  template <typename T>
  static command from_value(T pod) {
    command cmd;
    if constexpr (std::is_pod<T>::value) {
      cmd.data.resize(sizeof(pod));
      std::memcpy(cmd.data.data(), &pod, sizeof(pod));
    }

    static_assert(std::is_pod<T>::value, "!is_pod");
    return cmd;
  }

  template <typename T>
  T to_value() const {
    if constexpr (std::is_pod<T>::value) {
      assert(sizeof(T) == data.size());
      T pod;
      std::memcpy(&pod, data.data(), sizeof(pod));
      return pod;
    }
    static_assert(std::is_pod<T>::value, "!is_pod");
  }

  auto begin() -> decltype(this->data.begin()) { return data.begin(); }
  auto end() -> decltype(this->data.end()) { return data.end(); }
  auto cbegin() const -> decltype(this->data.cbegin()) { return data.cbegin(); }
  auto cend() const -> decltype(this->data.cend()) { return data.cend(); }

  EXPORT uint32_t crc() const;
};

} // namespace solidarity
