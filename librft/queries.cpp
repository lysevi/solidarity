#include <librft/queries.h>
#include <libutils/utils.h>

#include <libdialler/message.h>

using namespace rft::queries;

namespace {
msgpack::unpacker get_unpacker(const dialler::message_ptr &msg) {
  msgpack::unpacker pac;
  pac.reserve_buffer(msg->size());
  memcpy(pac.buffer(), msg->value(), msg->size());
  pac.buffer_consumed(msg->size());
  return pac;
}

template <typename... Args>
dialler::message_ptr pack_to_message(rft::queries::QUERY_KIND kind, Args &&... args) {
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> pk(&buffer);
  (pk.pack(args), ...);

  auto needed_size = (dialler::message::size_t)buffer.size();
  auto nd
      = std::make_shared<dialler::message>(needed_size, (dialler::message::kind_t)kind);

  memcpy(nd->value(), buffer.data(), buffer.size());
  return nd;
}

} // namespace

query_connect_t::query_connect_t(const dialler::message_ptr &msg) {
  ENSURE(msg->get_header()->kind
         == (dialler::message::kind_t)queries::QUERY_KIND::CONNECT);
  msgpack::unpacker pac = get_unpacker(msg);
  msgpack::object_handle oh;

  pac.next(oh);
  protocol_version = oh.get().as<uint16_t>();
  pac.next(oh);
  node_id = oh.get().as<std::string>();
}

dialler::message_ptr query_connect_t::query_connect_t::to_message() const {
  return pack_to_message(queries::QUERY_KIND::CONNECT, protocol_version, node_id);
}

connection_error_t::connection_error_t(const dialler::message_ptr &mptr) {
  ENSURE(mptr->get_header()->kind
         == (dialler::message::kind_t)queries::QUERY_KIND::CONNECTION_ERROR);
  msgpack::unpacker pac = get_unpacker(mptr);
  msgpack::object_handle oh;

  pac.next(oh);
  protocol_version = oh.get().as<uint16_t>();
  pac.next(oh);
  msg = oh.get().as<std::string>();
}

dialler::message_ptr connection_error_t::to_message() const {
  return pack_to_message(queries::QUERY_KIND::CONNECTION_ERROR, protocol_version, msg);
}

command_t::command_t(const dialler::message_ptr &mptr) {
  ENSURE(mptr->get_header()->kind
         == (dialler::message::kind_t)queries::QUERY_KIND::COMMAND);
  msgpack::unpacker pac = get_unpacker(mptr);
  msgpack::object_handle oh;

  pac.next(oh);
  auto byte_array = oh.get().as<std::vector<uint8_t>>();
  cmd = append_entries::from_byte_array(byte_array);
  pac.next(oh);
  auto m = oh.get().as<std::string>();
  from.set_name(m);
}

dialler::message_ptr command_t::to_message() const {
  return pack_to_message(queries::QUERY_KIND::COMMAND, cmd.to_byte_array(), from.name());
}