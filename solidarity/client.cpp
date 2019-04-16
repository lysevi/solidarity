#include <condition_variable>
#include <solidarity/client.h>
#include <solidarity/dialler/dialler.h>
#include <solidarity/protocol_version.h>
#include <solidarity/queries.h>
#include <solidarity/utils/strings.h>
#include <solidarity/utils/utils.h>

using namespace solidarity;

std::string solidarity::to_string(const client_event_t &cev) {
  std::stringstream ss;
  ss << "{kind:";
  switch (cev.kind) {
  case client_event_t::event_kind::NETWORK:
    ss << "NETWORK, ecode: " << to_string(cev.net_ev.value().ecode);
    break;
  case client_event_t::event_kind::RAFT:
    auto re = cev.raft_ev.value();
    ss << "RAFT, from:" << to_string(re.old_state)
       << " => to:" << to_string(re.new_state);
    break;
  case client_event_t::event_kind::STATE_MACHINE:
    ss << "STATE_MACHINE";
    break;
  default:
    NOT_IMPLEMENTED;
  }
  ss << "}";
  return ss.str();
}

class solidarity::async_result_t {
public:
  solidarity::async_result_t(uint64_t id_) {
    _id = id_;
    answer_received = false;
  }

  void wait() {
    std::unique_lock ul(_mutex);
    while (true) {
      _condition.wait(ul, [this] { return answer_received; });
      if (answer_received) {
        break;
      }
    }
  }

  std::vector<uint8_t> result() {
    wait();
    if (err.empty()) {
      return answer;
    }
    throw solidarity::exception(err);
  }

  void set_result(const std::vector<uint8_t> &r,
                  solidarity::ERROR_CODE ec,
                  const std::string &err_) {
    answer = r;
    _ec = ec;
    err = err_;
    answer_received = true;
    _condition.notify_all();
  }

  uint64_t id() const { return _id; }
  solidarity::ERROR_CODE ecode() const {
    ENSURE(_ec != solidarity::ERROR_CODE::UNDEFINED);
    return _ec;
  }

private:
  uint64_t _id;
  std::condition_variable _condition;
  std::mutex _mutex;
  std::vector<uint8_t> answer;
  std::string err;
  solidarity::ERROR_CODE _ec = solidarity::ERROR_CODE::UNDEFINED;
  bool answer_received;
};

void solidarity::inner::client_update_connection_status(client &c, bool status) {
  c._connected = status;
}

void solidarity::inner::client_update_async_result(client &c,
                                                   uint64_t id,
                                                   const std::vector<uint8_t> &cmd,
                                                   ERROR_CODE ec,
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
    ares->set_result(cmd, ec, err);
  }
}

void solidarity::inner::client_notify_update(client &c, const client_event_t &ev) {
  c.notify_on_update(ev);
}

class client_connection final : public solidarity::dialler::abstract_dial {
public:
  client_connection(client *const parent)
      : _parent(parent) {}

  void on_connect() override {
    queries::clients::client_connect_t qc(_parent->params().name, protocol_version);
    this->_connection->send_async(qc.to_message());
  }

  void on_new_message(solidarity::dialler::message_ptr &&d, bool & /*cancel*/) override {
    using namespace queries;
    QUERY_KIND kind = static_cast<QUERY_KIND>(d->get_header()->kind);
    switch (kind) {
    case QUERY_KIND::CONNECT: {
      // query_connect_t cc(d);
      inner::client_update_async_result(*_parent, 0, {}, ERROR_CODE::OK, std::string());
      break;
    }
    case QUERY_KIND::CONNECTION_ERROR: {
      connection_error_t cc(d);
      inner::client_update_async_result(*_parent, 0, {}, cc.status, cc.msg);
      break;
    }
    case QUERY_KIND::READ: {
      clients::read_query_t rq(d);
      inner::client_update_async_result(
          *_parent, rq.msg_id, rq.query.data, ERROR_CODE::OK, std::string());
      break;
    }
    case QUERY_KIND::STATUS: {
      status_t sq(d);
      // TODO check sq::status
      inner::client_update_async_result(
          *_parent, sq.id, std::vector<uint8_t>(), sq.status, sq.msg);
      break;
    }
    case QUERY_KIND::UPDATE: {
      state_machine_updated_event_t sme;
      client_event_t cev;
      cev.kind = client_event_t::event_kind::STATE_MACHINE;
      cev.state_ev = sme;
      inner::client_notify_update(*_parent, cev);
      break;
    }
    case QUERY_KIND::RAFT_STATE_UPDATE: {
      queries::clients::raft_state_updated_t rs(d);
      raft_state_event_t rse;
      rse.new_state = rs.new_state;
      rse.old_state = rs.old_state;

      client_event_t cev;
      cev.kind = client_event_t::event_kind::RAFT;
      cev.raft_ev = rse;
      inner::client_notify_update(*_parent, cev);
      break;
    }
    default:
      NOT_IMPLEMENTED;
    }
  }

  void on_network_error(const solidarity::dialler::message_ptr & /*d*/,
                        const boost::system::error_code & /*err*/) override {
    // TODO add message to log err.msg();
    inner::client_update_connection_status(*_parent, false);
    network_state_event_t ev;
    ev.ecode = ERROR_CODE::NETWORK_ERROR;

    client_event_t cev;
    cev.kind = client_event_t::event_kind::NETWORK;
    cev.net_ev = ev;
    inner::client_notify_update(*_parent, cev);
  }

private:
  client *const _parent;
};

client::client(const params_t &p)
    : _params(p)
    , _io_context((int)p.threads_count) {
  if (p.threads_count == 0) {
    throw solidarity::exception("threads count can't be zero");
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
  dialler::dial::params_t p(_params.host, _params.port, false);
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

ERROR_CODE client::send(const std::vector<uint8_t> &cmd) {
  if (!_connected) {
    return ERROR_CODE::NETWORK_ERROR;
  }
  solidarity::command c;
  c.data = cmd;

  auto waiter = make_waiter();
  queries::clients::write_query_t rq(waiter->id(), c);

  _dialler->send_async(rq.to_message());
  waiter->wait();
  auto result = waiter->ecode();
  return result;
}

std::vector<uint8_t> client::read(const std::vector<uint8_t> &cmd) {
  if (!_connected) {
    THROW_EXCEPTION("connection error.");
  }
  solidarity::command c;
  c.data = cmd;

  auto waiter = make_waiter();
  queries::clients::read_query_t rq(waiter->id(), c);

  _dialler->send_async(rq.to_message());

  return waiter->result();
}

uint64_t client::add_event_handler(const std::function<void(const client_event_t &)> &f) {
  std::lock_guard l(_locker);
  auto id = _next_query_id.fetch_add(1);
  _on_update_handlers[id] = f;
  return id;
}

void client::rm_event_handler(uint64_t id) {
  std::lock_guard l(_locker);
  if (auto it = _on_update_handlers.find(id); it != _on_update_handlers.end()) {
    _on_update_handlers.erase(it);
  }
}

void client::notify_on_update(const client_event_t &ev) {
  if (ev.kind == client_event_t::event_kind::NETWORK) {
    std::lock_guard l(_locker);
    for (auto &kv : _async_results) {
      kv.second->set_result({}, ev.net_ev.value().ecode, "");
    }
    _async_results.clear();
    _next_query_id.store(0);
  }
  std::shared_lock l(_locker);
  for (auto &kv : _on_update_handlers) {
    kv.second(ev);
  }
}
