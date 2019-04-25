#pragma once

#include <array>
#include <solidarity/command.h>

using namespace solidarity;

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

template <typename It>
unsigned int CRC32_function(It begin, It end) {
  constexpr auto crc_table = get_crc_table();
  unsigned long crc = 0xFFFFFFFFUL;
  for (auto it = begin; it != end; ++it) {
    crc = crc_table[(crc ^ *it) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFUL;
}

uint32_t command::crc() const{
  if (data.empty()) {
    return uint32_t();
  } else {
    return CRC32_function(data.cbegin(), data.cend());
  }
}