#pragma once

#include <array>

namespace solidarity::utils {

constexpr std::array<unsigned long, 256> get_crc_table() {
  std::array<unsigned long, 256> crc_table = {(unsigned long)0};
  unsigned long crc = 0;
  for (int i = 0; i < 256; i++) {
    crc = i;
    for (int j = 0; j < 8; j++) {
      crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320UL : crc >> 1;
    }
    crc_table[i] = crc;
  };
  return crc_table;
}

class crc32 {
public:
  void calculate(uint8_t t) {
    constexpr auto crc_table = get_crc_table();
    crc = crc_table[(crc ^ t) & 0xFF] ^ (crc >> 8);
  }
  template <class T>
  void calculate(const T *ptr, size_t size) {
    const uint8_t *begin = reinterpret_cast<const uint8_t *>(ptr);
    const uint8_t *end = begin + size;
    calculate(begin, end);
  }

  template <typename It>
  void calculate(It begin, It end) {
    for (auto it = begin; it != end; ++it) {
      calculate(*it);
    }
  }

  unsigned long checksum() const { return crc ^ 0xFFFFFFFFUL; }

private:
  unsigned long crc = 0xFFFFFFFFUL;
};

} // namespace solidarity::utils