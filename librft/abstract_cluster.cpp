#include <librft/abstract_cluster.h>
#include <libutils/strings.h>
#include <libutils/utils.h>

#include <msgpack.hpp>

namespace std {
std::string to_string(const rft::ENTRIES_KIND k) {
  switch (k) {
  case rft::ENTRIES_KIND::HEARTBEAT:
    return "HEARTBEAT";
  case rft::ENTRIES_KIND::VOTE:
    return "VOTE";
  case rft::ENTRIES_KIND::APPEND:
    return "APPEND";
  case rft::ENTRIES_KIND::ANSWER_OK:
    return "ANSWER_OK";
  case rft::ENTRIES_KIND::ANSWER_FAILED:
    return "ANSWER_FAILED";
  case rft::ENTRIES_KIND::HELLO:
    return "HELLO";
  }
  NOT_IMPLEMENTED
}

std::string to_string(const rft::append_entries &e) {
  return utils::strings::args_to_string("{cmd:",
                                        e.cmd.data.size(),
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

using namespace rft;

void pack_record_info(msgpack::packer<msgpack::sbuffer> &pk,
                      const logdb::reccord_info &ri) {
  pk.pack((uint8_t)ri.kind);
  pk.pack(ri.lsn);
  pk.pack(ri.term);
}

void unpack_record_info(msgpack::unpacker &pac, logdb::reccord_info &ri) {
  msgpack::object_handle oh;
  pac.next(oh);
  ri.kind = static_cast<rft::logdb::LOG_ENTRY_KIND>(oh.get().as<uint8_t>());
  pac.next(oh);
  ri.lsn = oh.get().as<rft::logdb::index_t>();
  pac.next(oh);
  ri.term = oh.get().as<rft::term_t>();
}

std::vector<uint8_t> append_entries::to_byte_array() const {
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> pk(&buffer);
  pk.pack(uint8_t(kind));
  pk.pack(term);
  pk.pack(starttime);
  pk.pack(leader.name());
  pk.pack(cmd.data);

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
  result.kind = static_cast<rft::ENTRIES_KIND>(oh.get().as<uint8_t>());
  pac.next(oh);
  result.term = oh.get().as<term_t>();
  pac.next(oh);
  result.starttime = oh.get().as<uint64_t>();
  pac.next(oh);
  auto leader = oh.get().as<std::string>();
  result.leader.set_name(leader);
  pac.next(oh);
  result.cmd.data = oh.get().as<std::vector<uint8_t>>();

  unpack_record_info(pac, result.current);
  unpack_record_info(pac, result.prev);
  unpack_record_info(pac, result.commited);
  return result;
}