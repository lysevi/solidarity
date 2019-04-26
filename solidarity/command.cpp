#include <array>
#include <solidarity/command.h>
#include <solidarity/utils/crc.h>

using namespace solidarity;

uint32_t command::crc() const {
  if (data.empty()) {
    return uint32_t();
  } else {
    return utils::crc(data.cbegin(), data.cend());
  }
}