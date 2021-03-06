#include <boost/asio.hpp>
#include <functional>
#include <solidarity/dialler/listener.h>
#include <solidarity/dialler/listener_client.h>
#include <string>

using namespace boost::asio;
using namespace boost::asio::ip;

using namespace solidarity::dialler;

listener_client::listener_client(uint64_t id_, async_io_ptr async_io, listener *s)
    : id(id_)
    , _listener(s) {
  _async_connection = async_io;
}

listener_client::~listener_client() {
  _async_connection = nullptr;
}

void listener_client::start() {
  initialisation_begin();
  auto self = shared_from_this();

  async_io::data_handler_t on_d = [self](std::vector<message_ptr> &d, bool &cancel) {
    self->on_data_recv(d, cancel);
  };

  async_io::error_handler_t on_n = [self](auto err) {
    self->on_network_error(err);
    self->close();
  };

  _async_connection->start(on_d, on_n);
  initialisation_complete();
}

void listener_client::close() {
  if (!is_stopping_started() && !is_stoped()) {
    stopping_started(true);
    if (_async_connection != nullptr) {
      _async_connection->full_stop();
      _async_connection = nullptr;
      this->_listener->erase_client_description(this->shared_from_this());
    }
  }
}

void listener_client::on_network_error(const boost::system::error_code &err) {
  this->_listener->on_network_error(this->shared_from_this(), err);
}

void listener_client::on_data_recv(std::vector<message_ptr> &d, bool &cancel) {
  _listener->on_new_message(this->shared_from_this(), d, cancel);
}

void listener_client::send_data(const message_ptr &d) {
  _async_connection->send(d);
}

void listener_client::send_data(const std::vector<message_ptr> &d) {
  _async_connection->send(d);
}