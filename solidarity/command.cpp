#include <array>
#include <msgpack.hpp>

#include <solidarity/command.h>
#include <solidarity/utils/crc.h>

using namespace solidarity;

uint32_t command_t::crc() const {
  if (data.empty()) {
    return uint32_t();
  } else {
    utils::crc32 result;
    result.calculate(data.begin(), data.end());
    result.calculate(&asm_num, sizeof(asm_num));
    return result.checksum();
  }
}

std::vector<uint8_t> command_t::to_byte_array() const {
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> pk(&buffer);
  pk.pack(data);
  pk.pack(asm_num);

  std::vector<uint8_t> result;
  result.resize(buffer.size());
  memcpy(result.data(), buffer.data(), buffer.size());
  return result;
}

command_t command_t::from_byte_array(const std::vector<uint8_t> &bytes) {
  command_t result;
  msgpack::unpacker pac;
  pac.reserve_buffer(bytes.size());
  memcpy(pac.buffer(), bytes.data(), bytes.size());
  pac.buffer_consumed(bytes.size());
  msgpack::object_handle oh;

  pac.next(oh);
  result.data = oh.get().as<std::vector<uint8_t>>();
  pac.next(oh);
  result.asm_num = oh.get().as<uint32_t>();

  return result;
}