#include "helpers.h"

#include <boost/asio.hpp>

#include <libdialler/dialler.h>
#include <libdialler/listener.h>

#include <catch2/catch.hpp>

#include <functional>
#include <string>
#include <thread>

namespace {

struct Listener final : public dialler::abstract_listener_consumer {
  Listener() { connections.store(0); }
  bool on_new_connection(dialler::listener_client_ptr) override {
    connections.fetch_add(1);
    return true;
  }

  void on_network_error(dialler::listener_client_ptr /*i*/,
                        const dialler::message_ptr & /*d*/,
                        const boost::system::error_code & /*err*/) override {}

  void on_new_message(dialler::listener_client_ptr /*i*/,
                      dialler::message_ptr && /*d*/,
                      bool & /*cancel*/) override {}

  void on_disconnect(const dialler::listener_client_ptr & /*i*/) override {
    connections.fetch_sub(1);
  }

  std::atomic_int16_t connections;
};

struct Connection final : public dialler::abstract_dial {
  void on_connect() override { mock_is_connected = true; };
  void on_new_message(dialler::message_ptr &&, bool &) override {}
  void on_network_error(const dialler::message_ptr &,
                        const boost::system::error_code &err) override {
    bool isError = err == boost::asio::error::operation_aborted
        || err == boost::asio::error::connection_reset || err == boost::asio::error::eof;
    if (isError && !is_stoped()) {
      auto msg = err.message();
      EXPECT_FALSE(true);
    }
  }

  bool mock_is_connected = false;
  bool connection_error = false;
};

bool server_stop = false;
std::shared_ptr<dialler::listener> server = nullptr;
std::shared_ptr<Listener> lstnr = nullptr;
boost::asio::io_context *context;

void server_thread() {
  dialler::listener::params_t p;
  p.port = 4040;
  context = new boost::asio::io_context();

  server = std::make_shared<dialler::listener>(context, p);
  lstnr = std::make_shared<Listener>();
  server->add_consumer(lstnr.get());

  server->start();
  while (!server_stop) {
    context->poll_one();
  }

  server->stop();
  context->stop();
  while (!context->stopped()) {
  }
  EXPECT_TRUE(context->stopped());
  delete context;
  server = nullptr;
}
} // namespace

TEST_CASE("listener.client", "[network]") {
  size_t clients_count = 0;
  dialler::dial::params_t p("localhost", 4040);

  SECTION("listener.client: 1") { clients_count = 1; }
  SECTION("listener.client: 10") { clients_count = 10; }

  server_stop = false;
  std::thread t(server_thread);
  while (server == nullptr || !server->is_started()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::vector<std::shared_ptr<dialler::dial>> clients(clients_count);
  std::vector<std::shared_ptr<Connection>> consumers(clients_count);
  for (size_t i = 0; i < clients_count; i++) {
    clients[i] = std::make_shared<dialler::dial>(context, p);
    consumers[i] = std::make_shared<Connection>();
    clients[i]->add_consumer(consumers[i].get());
    clients[i]->start_async_connection();
  }

  for (auto &c : consumers) {
    while (!c->mock_is_connected) {

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  while (!lstnr->is_started() && size_t(lstnr->connections.load()) != clients_count) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  for (auto &c : clients) {
    c->disconnect();
    while (!c->is_stoped()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  server_stop = true;
  while (server != nullptr) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  t.join();
}
