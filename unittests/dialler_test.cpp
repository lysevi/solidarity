#include "helpers.h"

#include <boost/asio.hpp>

#include <solidarity/dialler/dialler.h>
#include <solidarity/dialler/listener.h>

#include <catch.hpp>

#include <functional>
#include <string>
#include <thread>

namespace {

using namespace solidarity;

struct Listener final : public dialler::abstract_listener_consumer {
  Listener() {
    connections.store(0);
    recv_msg_count.store(0);
  }
  bool on_new_connection(dialler::listener_client_ptr) override {
    connections.fetch_add(1);
    return true;
  }

  void on_network_error(dialler::listener_client_ptr /*i*/,
                        const boost::system::error_code & /*err*/) override {}

  void on_new_message(dialler::listener_client_ptr /*i*/,
                      std::vector<dialler::message_ptr> &d,
                      bool & /*cancel*/) override {
    recv_msg_count += d.size();
  }

  void on_disconnect(const dialler::listener_client_ptr & /*i*/) override {
    connections.fetch_sub(1);
  }

  std::atomic_int16_t connections;
  std::atomic_size_t recv_msg_count;
};

struct Connection final : public dialler::abstract_dial {
  void on_connect() override { mock_is_connected = true; }
  void on_new_message(std::vector<dialler::message_ptr> &, bool &) override {}
  void on_network_error(const boost::system::error_code &err) override {
    bool isError = err == boost::asio::error::operation_aborted
                   || err == boost::asio::error::connection_reset
                   || err == boost::asio::error::eof;
    if (isError && !is_stoped()) {
      auto msg = err.message();
      EXPECT_FALSE(true);
    }
  }

  std::atomic_bool mock_is_connected = false;
  std::atomic_bool connection_error = false;
};

std::atomic_bool server_stop = false;
std::shared_ptr<dialler::listener> server = nullptr;
std::shared_ptr<Listener> lstnr = nullptr;
boost::asio::io_context *context;

std::atomic_bool server_is_started;

void server_thread() {
  server_is_started.store(false);

  dialler::listener::params_t p;
  p.port = 4040;
  context = new boost::asio::io_context();

  server = std::make_shared<dialler::listener>(context, p);
  lstnr = std::make_shared<Listener>();
  server->add_consumer(lstnr.get());

  server->start();
  server_is_started.store(true);
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
  server_is_started.store(false);
  lstnr = nullptr;
}
} // namespace

TEST_CASE("listener.client", "[network]") {
  size_t clients_count = 0;
  dialler::dial::params_t p("localhost", 4040);

  SECTION("listener.client: 1") { clients_count = 1; }
  SECTION("listener.client: 10") { clients_count = 10; }

  server_stop = false;
  std::thread t(server_thread);
  while (!server_is_started.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::vector<std::shared_ptr<dialler::dial>> clients(clients_count);
  std::vector<std::shared_ptr<Connection>> connections(clients_count);
  for (size_t i = 0; i < clients_count; i++) {
    clients[i] = std::make_shared<dialler::dial>(context, p);
    connections[i] = std::make_shared<Connection>();
    clients[i]->add_consumer(connections[i]);
    clients[i]->start_async_connection();
  }

  for (auto &c : connections) {
    while (!c->mock_is_connected) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  while (!lstnr->is_started() && size_t(lstnr->connections.load()) != clients_count) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  auto target_con = clients.front();
  auto m = std::make_shared<dialler::message>(10, dialler::message::kind_t(1));
  EXPECT_TRUE(m->get_header()->is_single_message());
  target_con->send_async(m);

  while (lstnr->recv_msg_count.load() != 1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::vector<dialler::message_ptr> messages(5);
  for (size_t i = 0; i < 5; ++i) {
    m = std::make_shared<dialler::message>(10, dialler::message::kind_t(1));
    if (i == 0) {
      m->get_header()->is_start_block = 1;
    } else {
      m->get_header()->is_piece_block = 1;
    }

    messages[i] = m;
  }
  messages.back()->get_header()->is_end_block = 1;
  messages.back()->get_header()->is_piece_block = 1;
  target_con->send_async(messages);

  while (lstnr->recv_msg_count.load() != messages.size() + 1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  for (auto &c : clients) {
    c->disconnect();
    while (!c->is_stoped()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  server_stop = true;
  while (server_is_started.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  t.join();
}
