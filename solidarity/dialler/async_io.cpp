#include <cassert>
#include <exception>

#include <solidarity/dialler/async_io.h>
#include <solidarity/utils/exception.h>
#include <solidarity/utils/utils.h>

using namespace boost::asio;
using namespace solidarity::dialler;

async_io::async_io(boost::asio::io_context *context)
    : _sock(*context)
    , next_message_size(0) {
  _begin_stoping_flag = false;
  _messages_to_send = 0;
  _is_stoped = true;
  assert(context != nullptr);
  _context = context;
}

async_io::~async_io() noexcept {
  full_stop();
}

void async_io::start(data_handler_t onRecv, error_handler_t onErr) {
  if (!_is_stoped) {
    return;
  }
  _on_recv_hadler = onRecv;
  _on_error_handler = onErr;
  _is_stoped = false;
  _begin_stoping_flag = false;
  read_next_async();
}

void async_io::full_stop(bool waitAllmessages) {
  if (_begin_stoping_flag) {
    return;
  }
  _begin_stoping_flag = true;
  
  try {
    if (_sock.is_open()) {
      if (waitAllmessages && _messages_to_send.load() != 0) {
        auto self = this->shared_from_this();
        boost::asio::post(_context->get_executor(), [self]() { self->full_stop(); });
      } else {
        boost::system::error_code ec;
        _sock.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        if (ec) {
          //#ifdef DOUBLE_CHECKS
          //          auto message = ec.message();
          //
          //          logger_fatal("AsyncIO::full_stop: _sock.shutdown() => code=",
          //          ec.value(),
          //                       " msg:", message);
          //#endif
        } else {
          _sock.close(ec);
          if (ec) {
            //#ifdef DOUBLE_CHECKS
            //            auto message = ec.message();
            //
            //            logger_fatal("AsyncIO::full_stop: _sock.close(ec)  => code=",
            //            ec.value(),
            //                         " msg:", message);
            //#endif
          }
        }
        _recv_message_pool.clear();
        next_message = nullptr;
        _on_recv_hadler = nullptr;
        _on_error_handler = nullptr;
        _context = nullptr;
        _is_stoped = true;
      }
    }

  } catch (...) {
  }
}

void async_io::send(const message_ptr d) {
  std::lock_guard l(_send_locker);
  if (_begin_stoping_flag) {
    return;
  }
  auto self = shared_from_this();

  auto ds = d->as_buffer();

  _messages_to_send.fetch_add(1);
  auto buf = boost::asio::buffer(ds.data, ds.size);

  auto on_write = [self](auto err, auto /*read_bytes*/) {
    if (err) {
      self->_on_error_handler(err);
    }
    self->_messages_to_send.fetch_sub(1);
  };
  async_write(_sock, buf, on_write);
}

void async_io::send(const std::vector<message_ptr> &d) {
  std::lock_guard l(_send_locker);
  for (auto &v : d) {
    if (_begin_stoping_flag) {
      return;
    }
    send(v);
  }
}

void async_io::read_next_async() {
  auto self = shared_from_this();

  auto on_read_size_clbk = [self](auto e, auto r) { self->on_read_size(e, r); };
  auto buf = boost::asio::buffer(static_cast<void *>(&self->next_message_size),
                                 message::SIZE_OF_SIZE);
  async_read(_sock, buf, on_read_size_clbk);
}

void async_io::on_read_size(boost::system::error_code err, size_t readed_bytes) {
  UNUSED(readed_bytes);
  if (_begin_stoping_flag) {
    return;
  }
  if (err) {
    _on_error_handler(err);
  } else {
    ENSURE(readed_bytes == message::SIZE_OF_SIZE);

    auto data_left = next_message_size - message::SIZE_OF_SIZE;
    next_message = std::make_shared<message>(next_message_size);

    auto buf_ptr = (uint8_t *)(next_message->get_header());
    auto buf = boost::asio::buffer(buf_ptr, data_left);
    auto self = shared_from_this();
    auto callback = [self](auto e, auto r) { self->on_read_message(e, r); };
    async_read(_sock, buf, callback);
  };
}

void async_io::on_read_message(boost::system::error_code err, size_t) {
  if (err) {
    _on_error_handler(err);
    next_message = nullptr;
    _recv_message_pool.clear();
  } else {
    _recv_message_pool.push_back(next_message);
    auto hdr = next_message->get_header();
    bool cancel_flag = false;
    if (hdr->is_single_message() || hdr->is_end_block) {
      try {
#ifdef DOUBLE_CHECKS
        auto first_kind = _recv_message_pool.front()->get_header()->kind;
        auto pred = [first_kind](const message_ptr &m) -> bool {
          return m->get_header()->kind == first_kind;
        };

        ENSURE(std::all_of(_recv_message_pool.cbegin(), _recv_message_pool.cend(), pred));
#endif
        _on_recv_hadler(_recv_message_pool, cancel_flag);
      } catch (std::exception &ex) {
        THROW_EXCEPTION("exception on async readNextAsync::on_read_message. - ",
                        ex.what());
      }
      _recv_message_pool.clear();
    }
    next_message = nullptr;
    if (!cancel_flag) {
      read_next_async();
    }
  }
}