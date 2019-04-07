#include <librft/client.h>
#include <librft/protocol_version.h>
#include <librft/queries.h>
#include <libdialler/dialler.h>
#include <libutils/strings.h>
#include <libutils/utils.h>
#include <boost/asio.hpp>
#include <condition_variable>

using namespace rft;

class rft::async_result_t {
public:
  rft::async_result_t(uint64_t id_) {
    _id = id_;
    answer_received = false;
  }

  std::vector<uint8_t> result() {
    std::unique_lock ul(_mutex);
    while (true) {
      _condition.wait(ul, [this] { return answer_received; });
      if (answer_received) {
        break;
      }
    }
    if (err.empty()) {
      return answer;
    }
    throw rft::exception(err);
  }

  void set_result(const std::vector<uint8_t> &r, const std::string &err_) {
    answer = r;
    err = err_;
    answer_received = true;
    _condition.notify_all();
  }

  uint64_t id() const { return _id; }

private:
  uint64_t _id;
  std::condition_variable _condition;
  std::mutex _mutex;
  std::vector<uint8_t> answer;
  std::string err;
  bool answer_received;
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
    ENSURE(id == ares->id());
    ares->set_result(cmd, err);
  }
}

void rft::inner::client_notify_update(client &c) {
  c.notify_on_update();
}

class client_connection : public dialler::abstract_dial {
public:
  client_connection(client *const parent)
      : _parent(parent) {}

  void on_connect() override {
    queries::clients::client_connect_t qc(_parent->params().name, protocol_version);
    this->_connection->send_async(qc.to_message());
  }

  void on_new_message(dialler::message_ptr &&d, bool &cancel) override {
    using namespace queries;
    QUERY_KIND kind = static_cast<QUERY_KIND>(d->get_header()->kind);
    switch (kind) {
    case QUERY_KIND::CONNECT: {
      // query_connect_t cc(d);
      inner::client_update_async_result(*_parent, 0, {}, std::string());
      break;
    }
    case QUERY_KIND::CONNECTION_ERROR: {
      connection_error_t cc(d);
      inner::client_update_async_result(*_parent, 0, {}, cc.msg);
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
      // TODO check sq::status
      inner::client_update_async_result(*_parent, sq.id, std::vector<uint8_t>(), sq.msg);
      break;
    }
    case QUERY_KIND::UPDATE: {
      inner::client_notify_update(*_parent);
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

client::client(const params_t &p)
    : _params(p)
    , _io_context((int)p.threads_count) {
  if (p.threads_count == 0) {
    throw rft::exception("threads count can't be zero");
  }
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
  _io_context.stop();
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
  // TODO clear _async_results and free all elements of them;

  _threads.resize(_params.threads_count);
  for (size_t i = 0; i < _params.threads_count; ++i) {
    _threads[i] = std::thread([this]() {
      _threads_at_work.fetch_add(1);
      while (true) {
        _io_context.run();
        if (_stoped) {
          break;
        }
        _io_context.restart();
      }
      _threads_at_work.fetch_sub(1);
    });
  }

  auto c = std::make_shared<client_connection>(this);
  dialler::dial::params_t p(_params.host, _params.port, true);
  _dialler = std::make_shared<dialler::dial>(&_io_context, p);
  _dialler->add_consumer(c);
  _dialler->start_async_connection();

  auto w = make_waiter();
  ENSURE(w->id() == 0);
  w->result();
  _connected = true;
}

std::shared_ptr<async_result_t> client::make_waiter() {
  std::lock_guard l(_locker);
  auto waiter = std::make_shared<async_result_t>(_next_query_id.fetch_add(1));
  _async_results[waiter->id()] = waiter;
  return waiter;
}

void client::send(const std::vector<uint8_t> &cmd) {
  rft::command c;
  c.data = cmd;

  auto waiter = make_waiter();
  queries::clients::write_query_t rq(waiter->id(), c);

  _dialler->send_async(rq.to_message());
}

std::vector<uint8_t> client::read(const std::vector<uint8_t> &cmd) {
  rft::command c;
  c.data = cmd;

  auto waiter = make_waiter();
  queries::clients::read_query_t rq(waiter->id(), c);

  _dialler->send_async(rq.to_message());

  return waiter->result();
}

uint64_t client::add_update_handler(const std::function<void()> &f) {
  std::lock_guard l(_locker);
  auto id = _next_query_id.fetch_add(1);
  _on_update_handlers[id] = f;
  return id;
}

void client::rm_update_handler(uint64_t id) {
  std::lock_guard l(_locker);
  if (auto it = _on_update_handlers.find(id); it != _on_update_handlers.end()) {
    _on_update_handlers.erase(it);
  }
}

void client::notify_on_update() {
  std::lock_guard l(_locker);
  for (auto &kv : _on_update_handlers) {
    kv.second();
  }
}