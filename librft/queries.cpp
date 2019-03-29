#include <librft/queries.h>
#include <libutils/utils.h>

#include <libdialler/message.h>

using namespace rft::queries;

query_connect_t::query_connect_t(const dialler::message_ptr &msg) {
  ENSURE(msg->get_header()->kind
         == (dialler::message::kind_t)queries::QUERY_KIND::CONNECT);
  msgpack::unpacker pac;
  pac.reserve_buffer(msg->size());
  memcpy(pac.buffer(), msg->value(), msg->size());
  pac.buffer_consumed(msg->size());
  msgpack::object_handle oh;

  pac.next(oh);
  protocol_version = oh.get().as<uint16_t>();
  pac.next(oh);
  node_id = oh.get().as<std::string>();
}

dialler::message_ptr query_connect_t::query_connect_t::to_message() const {
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> pk(&buffer);
  pk.pack(protocol_version);
  pk.pack(node_id);

  auto needed_size = (dialler::message::size_t)buffer.size();
  auto nd = std::make_shared<dialler::message>(
      needed_size, (dialler::message::kind_t)QUERY_KIND::CONNECT);

  memcpy(nd->value(), buffer.data(), buffer.size());
  return nd;
}

connection_error_t::connection_error_t(const dialler::message_ptr &mptr) {
  ENSURE(mptr->get_header()->kind
         == (dialler::message::kind_t)queries::QUERY_KIND::CONNECTION_ERROR);
  msgpack::unpacker pac;
  pac.reserve_buffer(mptr->size());
  memcpy(pac.buffer(), mptr->value(), mptr->size());
  pac.buffer_consumed(mptr->size());
  msgpack::object_handle oh;

  pac.next(oh);
  protocol_version = oh.get().as<uint16_t>();
  pac.next(oh);
  msg = oh.get().as<std::string>();
}

dialler::message_ptr connection_error_t::to_message() const {
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> pk(&buffer);
  pk.pack(protocol_version);
  pk.pack(msg);

  auto needed_size = (dialler::message::size_t)buffer.size();
  auto nd = std::make_shared<dialler::message>(
      needed_size, (dialler::message::kind_t)QUERY_KIND::CONNECTION_ERROR);

  memcpy(nd->value(), buffer.data(), buffer.size());
  return nd;
}
