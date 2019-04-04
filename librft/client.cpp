#include <librft/client.h>
#include <librft/protocol_version.h>
#include <librft/queries.h>
#include <libdialler/dialler.h>
#include <libutils/async/locker.h>
#include <libutils/strings.h>
#include <libutils/utils.h>
#include <boost/asio.hpp>

using namespace rft;

struct rft::async_result_t {
  uint64_t id;
  utils::async::locker locker;
  rft::command result;
  std::string err;
  void wait() { locker.lock(); }
};

void rft::inner::client_update_connection_status(client &c, bool status) {
  c._connected = status;
}

void rft::inner::client_update_async_result(client &c,
                                            uint64_t id,
                                            const std::vector<uint8_t> &cmd,
                                            const std::string &err) {
  c._locker.lock();
  if (auto it = c._async_results.find(id); it == c._async_results.end()) {
    c._locker.unlock();
    if (id != queries::UNDEFINED_QUERY_ID) {
      THROW_EXCEPTION("async result for id:", id, " not found!");
    }
  } else {
    auto ares = it->second;
    c._async_results.erase(it);
    c._locker.unlock();
    ares->result.data = cmd;
    ares->locker.unlock();
    ares->err = err;
    ENSURE(id == ares->id);
  }
}

class client_connection : public dialler::abstract_dial {
public:
  client_connection(client *const parent)
      : _parent(parent) {}

  void on_connect() override {
    queries::clients::client_connect_t qc(protocol_version);
    this->_connection->send_async(qc.to_message());
  }

  void on_new_message(dialler::message_ptr &&d, bool &cancel) override {
    using namespace queries;
    QUERY_KIND kind = static_cast<QUERY_KIND>(d->get_header()->kind);
    switch (kind) {
    case QUERY_KIND::CONNECT: {
      // query_connect_t cc(d);
      inner::client_update_connection_status(*_parent, true);
      break;
    }
    case QUERY_KIND::CONNECTION_ERROR: {
      connection_error_t cc(d);
      inner::client_update_connection_status(*_parent, false);
      auto msg = utils::strings::args_to_string("protocol version=",
                                                protocol_version,
                                                "remote protocol=",
                                                cc.protocol_version,
                                                " msg:",
                                                cc.msg);
      THROW_EXCEPTION(msg);
      break;
    }
    case QUERY_KIND::READ: {
      clients::read_query_t rq(d);
      inner::client_update_async_result(
          *_parent, rq.msg_id, rq.query.data, std::string());
      break;
    }
    case QUERY_KIND::STATUS: {
      status_t sq(d);
      inner::client_update_async_result(*_parent, sq.id, std::vector<uint8_t>(), sq.msg);
      break;
    }
    default:
      NOT_IMPLEMENTED;
    }
  }

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
  _next_query_id.store(0);
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

std::shared_ptr<async_result_t> client::make_waiter() {
  std::lock_guard l(_locker);
  auto waiter = std::make_shared<async_result_t>();
  waiter->id = _next_query_id.fetch_add(1);
  waiter->locker.lock();
  _async_results[waiter->id] = waiter;
  return waiter;
}

void client::send(const std::vector<uint8_t> &cmd) {
  rft::command c;
  c.data = cmd;

  auto waiter = make_waiter();
  queries::clients::write_query_t rq(waiter->id, c);

  _dialler->send_async(rq.to_message());
  /*
    waiter->wait();*/
}

std::vector<uint8_t> client::read(const std::vector<uint8_t> &cmd) {
  rft::command c;
  c.data = cmd;

  auto waiter = make_waiter();
  queries::clients::read_query_t rq(waiter->id, c);

  _dialler->send_async(rq.to_message());

  waiter->wait();
  return waiter->result.data;
}