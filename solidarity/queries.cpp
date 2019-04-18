#include <solidarity/queries.h>
#include <solidarity/utils/utils.h>

#include <numeric>
#include <solidarity/dialler/message.h>

#include <msgpack.hpp>

using namespace solidarity;
using namespace solidarity::queries;
using namespace solidarity::queries::clients;
using namespace solidarity::dialler;

namespace {

msgpack::unpacker get_unpacker(const uint8_t *data, message::size_t sz) {
  msgpack::unpacker pac;
  pac.reserve_buffer(sz);
  memcpy(pac.buffer(), data, sz);
  pac.buffer_consumed(sz);
  return pac;
}

msgpack::unpacker get_unpacker(const message_ptr &msg) {
  return get_unpacker(msg->value(), msg->values_size());
}

template <typename... Args>
message_ptr pack_to_message(solidarity::queries::QUERY_KIND kind, Args &&... args) {
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> pk(&buffer);
  (pk.pack(args), ...);

  auto needed_size = (message::size_t)buffer.size();
  auto nd = std::make_shared<message>(needed_size, (message::kind_t)kind);

  memcpy(nd->value(), buffer.data(), buffer.size());
  return nd;
}

void byte_array_to_msg(std::vector<dialler::message_ptr> &result,
                       message::kind_t mk,
                       const std::vector<uint8_t> &barray) {
  const auto max_buf_sz = message::MAX_BUFFER_SIZE;
  auto pos = barray.cbegin();
  while (pos != barray.cend()) {
    auto to_end = std::distance(pos, barray.cend());
    auto pos_end = pos;
    if (to_end >= max_buf_sz) {
      std::advance(pos_end, max_buf_sz);
    } else {
      pos_end = barray.cend();
    }

    auto m = std::make_shared<message>(message::size_t(std::distance(pos, pos_end)), mk);
    size_t i = 0;
    for (auto it = pos; it != pos_end; ++it, i++) {
      m->value()[i] = *it;
    }

    if (pos == barray.cbegin()) {
      m->get_header()->is_start_block = 1;
    } else {
      m->get_header()->is_piece_block = 1;
    }
    result.push_back(m);
    pos = pos_end;
  }
}


std::vector<uint8_t> messages_to_byte_array(const std::vector<message_ptr> &mptrs) {
  auto s = std::accumulate(mptrs.cbegin(),
                           mptrs.cend(),
                           size_t(0),
                           [](size_t s, const dialler::message_ptr &m) {
                             return s + size_t(m->values_size());
                           });
  s -= size_t(mptrs.back()->values_size());
  std::vector<uint8_t> buff(s);
  auto pos = buff.begin();
  for (auto it = mptrs.cbegin();; ++it) {
    auto n = std::next(it);
    if (n == mptrs.cend()) {
      break;
    }
    auto m = *it;
    pos = std::copy(m->value(), m->value() + m->values_size(), pos);
  }
  return buff;
}

} // namespace

query_connect_t::query_connect_t(const message_ptr &msg) {
  ENSURE(msg->get_header()->kind == (message::kind_t)queries::QUERY_KIND::CONNECT);
  msgpack::unpacker pac = get_unpacker(msg);
  msgpack::object_handle oh;

  pac.next(oh);
  protocol_version = oh.get().as<uint16_t>();
  pac.next(oh);
  node_id = oh.get().as<std::string>();
}

message_ptr query_connect_t::query_connect_t::to_message() const {
  return pack_to_message(queries::QUERY_KIND::CONNECT, protocol_version, node_id);
}

connection_error_t::connection_error_t(const message_ptr &mptr) {
  ENSURE(mptr->get_header()->kind
         == (message::kind_t)queries::QUERY_KIND::CONNECTION_ERROR);
  msgpack::unpacker pac = get_unpacker(mptr);
  msgpack::object_handle oh;

  pac.next(oh);
  protocol_version = oh.get().as<uint16_t>();
  pac.next(oh);
  msg = oh.get().as<std::string>();
  pac.next(oh);
  status = static_cast<ERROR_CODE>(oh.get().as<uint16_t>());
}

message_ptr connection_error_t::to_message() const {
  return pack_to_message(
      queries::QUERY_KIND::CONNECTION_ERROR, protocol_version, msg, (uint16_t)status);
}

status_t::status_t(const message_ptr &mptr) {
  ENSURE(mptr->get_header()->kind == (message::kind_t)queries::QUERY_KIND::STATUS);
  msgpack::unpacker pac = get_unpacker(mptr);
  msgpack::object_handle oh;

  pac.next(oh);
  id = oh.get().as<uint64_t>();
  pac.next(oh);
  msg = oh.get().as<std::string>();
  pac.next(oh);
  status = static_cast<ERROR_CODE>(oh.get().as<uint16_t>());
}

message_ptr status_t::to_message() const {
  return pack_to_message(
      queries::QUERY_KIND::STATUS, id, msg, static_cast<uint16_t>(status));
}


command_t::command_t(const std::vector<message_ptr> &mptrs) {
  ENSURE(std::all_of(mptrs.cbegin(), mptrs.cend(), [](auto mptr) {
    return mptr->get_header()->kind == (message::kind_t)QUERY_KIND::COMMAND;
  }));
  if (mptrs.size() == size_t(1)) {
    msgpack::unpacker pac = get_unpacker(mptrs.front());
    msgpack::object_handle oh;

    pac.next(oh);
    auto byte_array = oh.get().as<std::vector<uint8_t>>();
    cmd = append_entries::from_byte_array(byte_array);
    pac.next(oh);
    auto m = oh.get().as<std::string>();
    from.set_name(m);
  } else {
    msgpack::unpacker pac = get_unpacker(mptrs.back());
    msgpack::object_handle oh;

    pac.next(oh);
    auto name_str = oh.get().as<std::string>();
    from.set_name(name_str);

    auto buff = messages_to_byte_array(mptrs);
    cmd = append_entries::from_byte_array(buff);
  }
}

std::vector<message_ptr> command_t::to_message() const {
  using namespace dialler;
  auto barray = cmd.to_byte_array();
  auto total_size = barray.size() + from.name().size();
  std::vector<dialler::message_ptr> result;
  if (total_size < dialler::message::MAX_BUFFER_SIZE * 0.75) {
    result.resize(1);
    result[0] = pack_to_message(QUERY_KIND::COMMAND, barray, from.name());
  } else {
    byte_array_to_msg(result, (message::kind_t)QUERY_KIND::COMMAND, barray);
    auto m = pack_to_message(QUERY_KIND::COMMAND, from.name());
    m->get_header()->is_end_block = 1;
    result.push_back(m);
  }
  return result;
}

client_connect_t::client_connect_t(const message_ptr &msg) {
  ENSURE(msg->get_header()->kind == (message::kind_t)queries::QUERY_KIND::CONNECT);
  msgpack::unpacker pac = get_unpacker(msg);
  msgpack::object_handle oh;

  pac.next(oh);
  protocol_version = oh.get().as<uint16_t>();
  pac.next(oh);
  client_name = oh.get().as<std::string>();
}

message_ptr client_connect_t::to_message() const {
  return pack_to_message(queries::QUERY_KIND::CONNECT, protocol_version, client_name);
}

read_query_t::read_query_t(const std::vector<message_ptr> &mptrs) {
  ENSURE(std::all_of(mptrs.cbegin(), mptrs.cend(), [](auto mptr) {
    return mptr->get_header()->kind == (message::kind_t)QUERY_KIND::READ;
  }));
  if (mptrs.size() == size_t(1)) {
    msgpack::unpacker pac = get_unpacker(mptrs.front());
    msgpack::object_handle oh;

    pac.next(oh);
    msg_id = oh.get().as<uint64_t>();
    pac.next(oh);
    auto data = oh.get().as<std::vector<uint8_t>>();
    query.data = data;
  } else {
    msgpack::unpacker pac = get_unpacker(mptrs.back());
    msgpack::object_handle oh;

    pac.next(oh);
    msg_id = oh.get().as<uint64_t>();
    pac.next(oh);
    query.data = messages_to_byte_array(mptrs);
  }
}

std::vector<message_ptr> read_query_t::to_message() const {
  using namespace dialler;
  auto barray = query.data;
  auto total_size = barray.size() + sizeof(msg_id);
  std::vector<dialler::message_ptr> result;
  if (total_size < dialler::message::MAX_BUFFER_SIZE * 0.75) {
    result.resize(1);
    result[0] = pack_to_message(queries::QUERY_KIND::READ, msg_id, query.data);
  } else {
    byte_array_to_msg(result, (message::kind_t)QUERY_KIND::READ, barray);
    auto m = pack_to_message(QUERY_KIND::READ, msg_id);
    m->get_header()->is_end_block = 1;
    result.push_back(m);
  }
  return result;
}

write_query_t::write_query_t(const std::vector<dialler::message_ptr> &mptrs) {
  ENSURE(std::all_of(mptrs.cbegin(), mptrs.cend(), [](auto mptr) {
    return mptr->get_header()->kind == (message::kind_t)QUERY_KIND::WRITE;
  }));

  if (mptrs.size() == size_t(1)) {
    msgpack::unpacker pac = get_unpacker(mptrs.front());
    msgpack::object_handle oh;

    pac.next(oh);
    msg_id = oh.get().as<uint64_t>();
    pac.next(oh);
    auto data = oh.get().as<std::vector<uint8_t>>();
    query.data = data;
  } else {
    msgpack::unpacker pac = get_unpacker(mptrs.back());
    msgpack::object_handle oh;

    pac.next(oh);
    msg_id = oh.get().as<uint64_t>();

    query.data = messages_to_byte_array(mptrs);
  }
}

std::vector<dialler::message_ptr> write_query_t::to_message() const {
  // return {pack_to_message(queries::QUERY_KIND::WRITE, msg_id, query.data)};
  using namespace dialler;
  auto barray = query.data;
  auto total_size = barray.size() + sizeof(msg_id);
  std::vector<dialler::message_ptr> result;
  if (total_size < dialler::message::MAX_BUFFER_SIZE * 0.75) {
    result.resize(1);
    result[0] = pack_to_message(QUERY_KIND::WRITE, msg_id, query.data);
  } else {
    byte_array_to_msg(result, (message::kind_t)QUERY_KIND::WRITE, barray);
    auto m = pack_to_message(QUERY_KIND::WRITE, msg_id);
    m->get_header()->is_end_block = 1;
    result.push_back(m);
  }
  return result;
}

state_machine_updated_t::state_machine_updated_t() {
  f = true;
}

state_machine_updated_t::state_machine_updated_t(const message_ptr &msg) {
  ENSURE(msg->get_header()->kind == (message::kind_t)queries::QUERY_KIND::UPDATE);
  msgpack::unpacker pac = get_unpacker(msg);
  msgpack::object_handle oh;

  pac.next(oh);
  f = oh.get().as<bool>();
}

message_ptr state_machine_updated_t::to_message() const {
  return pack_to_message(queries::QUERY_KIND::UPDATE, f);
}

raft_state_updated_t::raft_state_updated_t(NODE_KIND f, NODE_KIND t) {
  old_state = f;
  new_state = t;
}

raft_state_updated_t::raft_state_updated_t(const message_ptr &msg) {
  ENSURE(msg->get_header()->kind
         == (message::kind_t)queries::QUERY_KIND::RAFT_STATE_UPDATE);
  msgpack::unpacker pac = get_unpacker(msg);
  msgpack::object_handle oh;

  pac.next(oh);
  old_state = static_cast<NODE_KIND>(oh.get().as<uint8_t>());

  pac.next(oh);
  new_state = static_cast<NODE_KIND>(oh.get().as<uint8_t>());
}

message_ptr raft_state_updated_t::to_message() const {
  return pack_to_message(queries::QUERY_KIND::RAFT_STATE_UPDATE,
                         static_cast<uint8_t>(old_state),
                         static_cast<uint8_t>(new_state));
}