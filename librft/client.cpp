#include <librft/client.h>
#include <libdialler/dialler.h>
#include <libutils/strings.h>
#include <libutils/utils.h>

#include <boost/asio.hpp>

using namespace rft;

void rft::inner::client_update_connection_status(client &c, bool status) {
  c._connected = status;
}

class client_connection : public dialler::abstract_dial {
public:
  client_connection(client *const parent)
      : _parent(parent) {}

  void on_connect() override { inner::client_update_connection_status(*_parent, true); }

  void on_new_message(dialler::message_ptr &&d, bool &cancel) override {}

  void on_network_error(const dialler::message_ptr &d,
                        const boost::system::error_code &err) override {
    inner::client_update_connection_status(*_parent, false);
  }

private:
  client *const _parent;
};

client::client(const params_t &p) {
  _params = p;
  _connected = false;
}

client::~client() {
  disconnect();
}

void client::disconnect() {
  if (_threads.empty()) {
    return;
  }

  _stoped = true;
  while (_threads_at_work.load() != 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  for (auto &&t : _threads) {
    t.join();
  }
  _threads.clear();
  _dialler->disconnect();
  _dialler->wait_stoping();
  _connected = false;
}

void client::connect() {
  if (_dialler != nullptr) {
    throw utils::exceptions::exception_t("called twice!");
  }
  _threads_at_work.store(0);

  _threads.resize(_params.threads_count);
  for (size_t i = 0; i < _params.threads_count; ++i) {
    _threads[i] = std::thread([this]() {
      _threads_at_work.fetch_add(1);
      while (!_stoped) {
        _io_context.poll_one();
      }
      _threads_at_work.fetch_sub(1);
    });
  }

  auto c = std::make_shared<client_connection>(this);
  dialler::dial::params_t p(_params.host, _params.port, true);
  _dialler = std::make_shared<dialler::dial>(&_io_context, p);
  _dialler->add_consumer(c);
  _dialler->start_async_connection();
}