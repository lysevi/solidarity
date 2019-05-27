#include <solidarity/async_result.h>
#include <solidarity/client.h>
#include <solidarity/client_exception.h>
#include <solidarity/dialler/dialler.h>
#include <solidarity/protocol_version.h>
#include <solidarity/queries.h>
#include <solidarity/utils/strings.h>
#include <solidarity/utils/utils.h>

using namespace solidarity;

void solidarity::inner::client_update_connection_status(client &c, bool status) {
  c._connected = status;
}

void solidarity::inner::client_update_async_result(client &c,
                                                   uint64_t id,
                                                   const std::vector<uint8_t> &cmd,
                                                   ERROR_CODE ec,
                                                   const std::string &err) {
  auto ares = c._arh.get_waiter(id);
  c._arh.erase_waiter(id);
  ENSURE(id == ares->id());
  ares->set_result(cmd, ec, err);
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

  void on_new_message(std::vector<dialler::message_ptr> &d, bool & /*cancel*/) override {
    using namespace queries;
    QUERY_KIND kind = static_cast<QUERY_KIND>(d.front()->get_header()->kind);
    switch (kind) {
    case QUERY_KIND::CONNECT: {
      // query_connect_t cc(d);
      inner::client_update_async_result(*_parent, 0, {}, ERROR_CODE::OK, std::string());
      break;
    }
    case QUERY_KIND::CONNECTION_ERROR: {
      connection_error_t cc(d.front());
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
      status_t sq(d.front());
      // TODO check sq::status
      inner::client_update_async_result(
          *_parent, sq.id, std::vector<uint8_t>(), sq.status, sq.msg);
      break;
    }
    case QUERY_KIND::COMMAND_STATUS: {
      queries::clients::command_status_query_t smuq(d.front());
      client_event_t cev;
      cev.kind = client_event_t::event_kind::COMMAND_STATUS;
      cev.cmd_ev = smuq.e;
      inner::client_notify_update(*_parent, cev);
      break;
    }
    case QUERY_KIND::RAFT_STATE_UPDATE: {
      queries::clients::raft_state_updated_t rs(d.front());
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

  void on_network_error(const boost::system::error_code & /*err*/) override {
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
}

client::~client() {
  disconnect();
}

void client::disconnect() {
  std::lock_guard l(_connect_locker);

  if (_stoped.load()) {
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
  _dialler = nullptr;
  _connected = false;
}

void client::connect() {
  std::lock_guard l(_connect_locker);
  if (_stoped.load()) {
    return;
  }

  if (_dialler != nullptr) {
    throw utils::exceptions::exception_t("called twice!");
  }
  _threads_at_work.store(0);

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

  auto w = _arh.make_waiter();
  ENSURE(w->id() == 0);
  UNUSED(w->result());
  _connected = true;
}

ERROR_CODE client::send_weak(const solidarity::command &cmd) {
  if (!_connected) {
    return ERROR_CODE::NETWORK_ERROR;
  }

  auto waiter = _arh.make_waiter();
  queries::clients::write_query_t rq(waiter->id(), cmd);
  auto messages = rq.to_message();
  _dialler->send_async(messages);
  waiter->wait();
  auto result = waiter->ecode();
  return result;
}

solidarity::send_result client::send_strong(const solidarity::command &cmd) {
  auto crc = cmd.crc();
  solidarity::send_result result;

  struct waiter_t {
    std::mutex locker;
    std::condition_variable cond;
    std::atomic_bool is_end = false;
    command_status status = command_status::APPLY_ERROR;
  };

  auto w = std::make_shared<waiter_t>();

  auto uh_id = add_event_handler([w, crc](const client_event_t &cev) {
    if (cev.kind == client_event_t::event_kind::COMMAND_STATUS) {
      auto cmd_status = cev.cmd_ev.value();
      if (cmd_status.crc == crc) {
        if (cmd_status.status == command_status::WAS_APPLIED
            || cmd_status.status == command_status::CAN_NOT_BE_APPLY
            || cmd_status.status == command_status::APPLY_ERROR
            || cmd_status.status == command_status::ERASED_FROM_JOURNAL) {
          {
            std::lock_guard l(w->locker);
            w->status = cmd_status.status;
            w->is_end.store(true);
            w->cond.notify_all();
          }
        }
      }
    }
  });

  result.ecode = send_weak(cmd);
  if (result.ecode != ERROR_CODE::OK) {
    rm_event_handler(uh_id);
    return result;
  }

  std::unique_lock ulock(w->locker);
  while (!w->is_end) {
    w->cond.wait(ulock, [w]() { return w->is_end.load(); });
    if (w->is_end) {
      break;
    }
  }
  rm_event_handler(uh_id);

  result.status = w->status;
  return result;
}

solidarity::command client::read(const solidarity::command &cmd) {
  if (!_connected) {
    THROW_EXCEPTION("connection error.");
  }

  auto waiter = _arh.make_waiter();
  queries::clients::read_query_t rq(waiter->id(), cmd);

  _dialler->send_async(rq.to_message());
  solidarity::command c;
  c.data = waiter->result();
  return c;
}

uint64_t client::add_event_handler(const std::function<void(const client_event_t &)> &f) {
  std::lock_guard l(_locker);
  auto id = _arh.get_next_id();
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
  std::unordered_map<uint64_t, std::function<void(const client_event_t &)>> copied;
  if (ev.kind == client_event_t::event_kind::NETWORK) {
    std::lock_guard l(_locker);
    _arh.clear(ev.net_ev.value().ecode);
  }
  {
    std::lock_guard l(_locker);
    if (!_on_update_handlers.empty()) {
      copied.reserve(_on_update_handlers.size());
      copied.insert(_on_update_handlers.cbegin(), _on_update_handlers.cend());
    }
  }
  for (auto &kv : copied) {
    kv.second(ev);
  }
}
