#include <librft/queries.h>
#include <libutils/utils.h>

#include <libdialler/message.h>
#include <numeric>

using namespace rft::queries;

namespace {

msgpack::unpacker get_unpacker(const uint8_t *data, dialler::message::size_t sz) {
  msgpack::unpacker pac;
  pac.reserve_buffer(sz);
  memcpy(pac.buffer(), data, sz);
  pac.buffer_consumed(sz);
  return pac;
}

msgpack::unpacker get_unpacker(const dialler::message_ptr &msg) {
  return get_unpacker(msg->value(), msg->values_size());
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

command_t::command_t(const std::vector<dialler::message_ptr> &mptrs) {
  ENSURE(std::all_of(mptrs.cbegin(), mptrs.cend(), [](auto mptr) {
    return mptr->get_header()->kind == (dialler::message::kind_t)QUERY_KIND::COMMAND;
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
    auto m = oh.get().as<std::string>();
    from.set_name(m);

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
    cmd = append_entries::from_byte_array(buff);
  }
}

std::vector<dialler::message_ptr> command_t::to_message() const {
  using namespace dialler;
  auto barray = cmd.to_byte_array();
  auto total_size = barray.size() + from.name().size();
  std::vector<dialler::message_ptr> result;
  if (total_size < dialler::message::MAX_BUFFER_SIZE * 0.75) {
    result.resize(1);
    result[0] = pack_to_message(QUERY_KIND::COMMAND, barray, from.name());
  } else {
    // auto pieces_count = total_size % dialler::message::MAX_BUFFER_SIZE + 1;
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

      auto m = std::make_shared<message>(message::size_t(std::distance(pos, pos_end)),
                                         (message::kind_t)QUERY_KIND::COMMAND);
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
    auto m = pack_to_message(QUERY_KIND::COMMAND, from.name());
    m->get_header()->is_end_block = 1;
    result.push_back(m);
  }
  return result;
}

client_connect_t::client_connect_t(const dialler::message_ptr &msg) {
  ENSURE(msg->get_header()->kind
         == (dialler::message::kind_t)queries::QUERY_KIND::CONNECT);
  msgpack::unpacker pac = get_unpacker(msg);
  msgpack::object_handle oh;

  pac.next(oh);
  protocol_version = oh.get().as<uint16_t>();
}

dialler::message_ptr client_connect_t::to_message() const {
  return pack_to_message(queries::QUERY_KIND::CONNECT, protocol_version);
}
