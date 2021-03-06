#pragma once

#include <solidarity/exports.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace solidarity {

struct command_t {
  command_t() = default;
  command_t(const command_t &) = default;
  command_t(command_t &&) = default;
  command_t(const std::initializer_list<std::uint8_t> &il)
      : data(il) {}
  command_t(const std::vector<std::uint8_t> &o)
      : data(o) {}
  command_t(const size_t sz)
      : data(sz) {}

  command_t(uint32_t asm_number, const std::initializer_list<std::uint8_t> &il)
      : data(il)
      , asm_num(asm_number) {}
  command_t(uint32_t asm_number, const std::vector<std::uint8_t> &o)
      : data(o)
      , asm_num(asm_number) {}
  command_t(uint32_t asm_number, const size_t sz)
      : data(sz)
      , asm_num(asm_number) {}

  command_t &operator=(const command_t &) = default;

  std::vector<std::uint8_t> data;
  /// abstract state machine id
  uint32_t asm_num = 0;

  bool is_empty() const { return data.empty(); }
  size_t size() const { return data.size(); }
  void resize(const size_t s) { return data.resize(s); }

  template <typename T>
  static command_t from_value(T pod) {
    command_t cmd;
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
  [[nodiscard]] EXPORT std::vector<uint8_t> to_byte_array() const;
  [[nodiscard]] EXPORT static command_t from_byte_array(const std::vector<uint8_t> &bytes);
};

} // namespace solidarity
