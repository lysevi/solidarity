#include <solidarity/append_entries.h>
#include <solidarity/utils/strings.h>
#include <solidarity/utils/utils.h>

#include <msgpack.hpp>
#include <memory>

namespace std {
std::string to_string(const solidarity::ENTRIES_KIND k) {
  switch (k) {
  case solidarity::ENTRIES_KIND::HEARTBEAT:
    return "HEARTBEAT";
  case solidarity::ENTRIES_KIND::VOTE:
    return "VOTE";
  case solidarity::ENTRIES_KIND::APPEND:
    return "APPEND";
  case solidarity::ENTRIES_KIND::ANSWER_OK:
    return "ANSWER_OK";
  case solidarity::ENTRIES_KIND::ANSWER_FAILED:
    return "ANSWER_FAILED";
  case solidarity::ENTRIES_KIND::HELLO:
    return "HELLO";
  }
  NOT_IMPLEMENTED
}

std::string to_string(const solidarity::append_entries &e) {
  return solidarity::utils::strings::to_string("{cmd:",
                                               e.cmd.size(),
                                               " kind:",
                                               e.kind,
                                               " L:",
                                               e.leader,
                                               " term:",
                                               e.term,
                                               " cur:",
                                               e.current,
                                               " prev:",
                                               e.prev,
                                               " ci:",
                                               e.commited,
                                               "}");
}
} // namespace std

using namespace solidarity;


void pack_record_info(msgpack::packer<msgpack::sbuffer> &pk,
                      const logdb::reccord_info &ri) {
  pk.pack((uint8_t)ri.kind);
  pk.pack(ri.lsn);
  pk.pack(ri.term);
}

void unpack_record_info(msgpack::unpacker &pac, logdb::reccord_info &ri) {
  msgpack::object_handle oh;
  pac.next(oh);
  ri.kind = static_cast<solidarity::logdb::LOG_ENTRY_KIND>(oh.get().as<uint8_t>());
  pac.next(oh);
  ri.lsn = oh.get().as<solidarity::index_t>();
  pac.next(oh);
  ri.term = oh.get().as<solidarity::term_t>();
}

std::vector<uint8_t> append_entries::to_byte_array() const {
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> pk(&buffer);
  pk.pack(uint8_t(kind));
  pk.pack(term);
  pk.pack(starttime);
  pk.pack(leader);
  pk.pack(cmd.to_byte_array());

  pack_record_info(pk, current);
  pack_record_info(pk, prev);
  pack_record_info(pk, commited);
  std::vector<uint8_t> result;
  result.resize(buffer.size());
  memcpy(result.data(), buffer.data(), buffer.size());
  return result;
}

append_entries append_entries::from_byte_array(const std::vector<uint8_t> &bytes) {
  append_entries result;
  msgpack::unpacker pac;
  pac.reserve_buffer(bytes.size());
  memcpy(pac.buffer(), bytes.data(), bytes.size());
  pac.buffer_consumed(bytes.size());
  msgpack::object_handle oh;

  pac.next(oh);
  result.kind = static_cast<solidarity::ENTRIES_KIND>(oh.get().as<uint8_t>());
  pac.next(oh);
  result.term = oh.get().as<term_t>();
  pac.next(oh);
  result.starttime = oh.get().as<uint64_t>();
  pac.next(oh);
  result.leader = oh.get().as<std::string>();
  pac.next(oh);
  result.cmd = command_t::from_byte_array(oh.get().as<std::vector<uint8_t>>());

  unpack_record_info(pac, result.current);
  unpack_record_info(pac, result.prev);
  unpack_record_info(pac, result.commited);
  return result;
}