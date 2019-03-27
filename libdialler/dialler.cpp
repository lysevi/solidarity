#include <boost/asio.hpp>

#include <libdialler/dialler.h>

using namespace dialler;

abstract_dial ::~abstract_dial() {
  _connection->erase_consumer();
}

bool abstract_dial::is_connected() const {
  return _connection->is_started();
}

bool abstract_dial::is_stoped() const {
  return _connection->is_stopping_started();
}

void abstract_dial::add_connection(std::shared_ptr<dial> c) {
  _connection = c;
}

dial::dial(boost::asio::io_context *context, const params_t &params)
    : _context(context)
    , _params(params)
    , _consumers() {}

dial::~dial() {
  disconnect();
}

void dial::disconnect() {
  if (!is_stoped()) {
    stopping_started();
    _async_io->fullStop();
    stopping_completed();
  }
}

void dial::reconnecton_error(const message_ptr &d, const boost::system::error_code &err) {

  {
    if (_consumers != nullptr) {
      _consumers->on_network_error(d, err);
    }
  }

  if (!is_stopping_started() && !is_stoped() && _params.auto_reconnection) {
    this->start_async_connection();
  }
}

void dial::start_async_connection() {
  if (!is_initialisation_begin()) {
    initialisation_begin();
  }

  using namespace boost::asio::ip;
  tcp::resolver resolver(*_context);
  auto const results = resolver.resolve(_params.host, std::to_string(_params.port));

  auto self = this->shared_from_this();
  self->_async_io = std::make_shared<async_io>(self->_context);

  auto con_handler = [self](auto ec, auto resoler_ir) {
    if (ec) {
      if (!self->is_stoped()) {
        self->reconnecton_error(nullptr, ec);
      }
    } else {

      if (self->_async_io->socket().is_open()) {
        async_io::data_handler_t on_d = [self](auto d, auto cancel) {
          self->on_data_receive(std::move(d), cancel);
        };
        async_io::error_handler_t on_n
            = [self](auto d, auto err) { self->reconnecton_error(d, err); };

        self->_async_io->start(on_d, on_n);

        if (self->_consumers != nullptr) {
          self->_consumers->on_connect();
        }
        self->initialisation_complete();
      }
    }
  };

  boost::asio::async_connect(
      self->_async_io->socket(), results.begin(), results.end(), con_handler);
}

void dial::on_data_receive(message_ptr &&d, bool &cancel) {
  {
    if (_consumers != nullptr) {
      _consumers->on_new_message(std::move(d), cancel);
    }
  }
}

void dial::send_async(const message_ptr &d) {
  if (_async_io) {
    _async_io->send(d);
  }
}

void dial::add_consumer(const abstract_connection_consumer_ptr &c) {
  _consumers = c;
  c->add_connection(shared_from_this());
}

void dial::erase_consumer() {
  _consumers = nullptr;
}