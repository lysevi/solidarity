#include <solidarity/mesh_connection.h>
#include <solidarity/queries.h>
#include <solidarity/utils/utils.h>

using namespace solidarity;

namespace solidarity::impl {
out_connection::out_connection(const std::shared_ptr<mesh_connection> parent,
                               const std::string &target_addr) {
  _parent = parent;
  _target_addr = target_addr;
}

void out_connection::on_connect() {
  _parent->_logger->info("connect to ", _target_addr);
  queries::query_connect_t qc(protocol_version, _parent->_self_addr);
  this->_connection->send_async(qc.to_message());
}

void out_connection::on_new_message(std::vector<dialler::message_ptr> &d, bool &cancel) {
  using namespace queries;
  QUERY_KIND kind = static_cast<QUERY_KIND>(d.front()->get_header()->kind);
  switch (kind) {
  case QUERY_KIND::CONNECT: {
    query_connect_t qc(d.front());
    _parent->accept_out_connection(node_name(qc.node_id), _target_addr);
    break;
  }
  case QUERY_KIND::CONNECTION_ERROR: {
    _parent->rm_out_connection(_target_addr, boost::asio::error::access_denied);
    cancel = true;
    break;
  }
  default:
    break;
  }
}

void out_connection::on_network_error(const boost::system::error_code &err) {
  _parent->rm_out_connection(_target_addr, err);
}

listener::listener(const std::shared_ptr<mesh_connection> parent) {
  _parent = parent;
}

void listener::on_network_error(dialler::listener_client_ptr i,
                                const boost::system::error_code &err) {
  _parent->rm_input_connection(i->get_id(), err);
}

void listener::on_new_message(dialler::listener_client_ptr i,
                              std::vector<dialler::message_ptr> &d,
                              bool &cancel) {
  using namespace queries;
  QUERY_KIND kind = static_cast<QUERY_KIND>(d.front()->get_header()->kind);
  switch (kind) {
  case QUERY_KIND::CONNECT: {
    query_connect_t qc(d.front());
    dialler::message_ptr dout;
    if (qc.protocol_version != protocol_version) {
      dout = connection_error_t(protocol_version,
                                solidarity::ERROR_CODE::WRONG_PROTOCOL_VERSION,
                                "protocol version error")
                 .to_message();
      cancel = true;
    } else {
      dout = query_connect_t(protocol_version, _parent->_self_addr).to_message();
      auto addr = qc.node_id;
      _parent->accept_input_connection(addr, i->get_id());
      _parent->_logger->info("accept connection from ", addr);
    }
    i->send_data(dout);
    break;
  }
  case QUERY_KIND::COMMAND: {
    _parent->on_new_command(d);
    break;
  }
  case QUERY_KIND::RESEND: {
    queries::resend_query_t rsq(d);
    _parent->on_query_resend(
        _parent->addr_by_id(i->get_id()), rsq.msg_id, rsq.kind, rsq.query);
    break;
  }
  case QUERY_KIND::STATUS: {
    queries::status_t sq(d.front());
    _parent->on_write_status(sq.id, sq.status);
    break;
  }
  case queries::QUERY_KIND::COMMAND_STATUS: {
    queries::clients::command_status_query_t smuq(d.front());
    if (_parent->_on_smue_handler != nullptr) {
      _parent->_on_smue_handler(smuq.e);
    }
    break;
  }
  case queries::QUERY_KIND::CLUSTER_STATUS: {
    queries::cluster_status_t cstat(d);
    auto r = _parent->_ash.get_waiter(cstat.msg_id);

    cluster_state_event_t cse;
    cse.leader = cstat.leader;

    for (auto &&kv : std::move(cstat.state)) {
      cse.state.insert(std::pair(kv.first, kv.second));
    }

    r->set_result(cse, ERROR_CODE::OK, "");
    _parent->_ash.erase_waiter(cstat.msg_id);
    break;
  }
  default:
    NOT_IMPLEMENTED;
  }
} // namespace solidarity::impl

bool listener::on_new_connection(dialler::listener_client_ptr) {
  return true;
}

void listener::on_disconnect(const dialler::listener_client_ptr &i) {
  _parent->rm_input_connection(i->get_id(), boost::asio::error::connection_aborted);
}

} // namespace solidarity::impl

mesh_connection::mesh_connection(node_name self_addr,
                                 const std::shared_ptr<abstract_cluster_client> &client,
                                 const utils::logging::abstract_logger_ptr &logger,
                                 const mesh_connection::params_t &params)
    : _stoped(true)
    , _io_context((int)params.thread_count) {

  if (params.thread_count == 0) {
    THROW_EXCEPTION("threads count is zero!");
  }
  _threads_at_work.store(0);
  _logger = std::make_shared<utils::logging::prefix_logger>(logger, "[network] ");
  _client = client;
  _params = params;
  _self_addr = self_addr;
}

mesh_connection::~mesh_connection() {
  stop();
  _logger = nullptr;
}

void mesh_connection::start() {
  std::lock_guard l(_locker);
  _stoped = false;
  _evl_stoped = false;
  _threads.resize(_params.thread_count);
  for (size_t i = 0; i < _params.thread_count; ++i) {
    _threads[i] = std::thread([this]() {
      _threads_at_work.fetch_add(1);
      while (true) {
        try {
          _io_context.run();
          if (_evl_stoped) {
            break;
          }
          _io_context.restart();
        } catch (std::exception &ex) {
          this->_logger->fatal("mesh_connection loop: ", ex.what());
        }
      }
      _threads_at_work.fetch_sub(1);
    });
  }

  auto self = shared_from_this();
  _listener_consumer = std::make_shared<impl::listener>(self);
  _listener = std::make_unique<dialler::listener>(&_io_context, _params.listener_params);
  _listener->add_consumer(_listener_consumer.get());
  _listener->start();
  _listener->wait_starting();

  for (auto &p : _params.addrs) {

    auto cnaddr = utils::strings::to_string(p.host + ":" + std::to_string(p.port));
    auto c = std::make_shared<impl::out_connection>(self, cnaddr);

    ENSURE(_diallers.find(cnaddr) == _diallers.end());
    ENSURE(!cnaddr.empty());

    auto d = std::make_shared<dialler::dial>(&_io_context, p);
    d->add_consumer(c);
    _diallers.insert(std::make_pair(cnaddr, d));
    d->start_async_connection();
  }
}

void mesh_connection::stop() {
  _logger->info("stoping...");

  stop_event_loop();

  for (auto &&kv : _diallers) {
    kv.second->disconnect();
    kv.second->wait_stoping();
  }
  _diallers.clear();

  if (_listener != nullptr) {
    _listener->stop();
    _listener->wait_stoping();
    _listener = nullptr;
    _listener_consumer = nullptr;
  }
  _client = nullptr;
  _ash.clear(ERROR_CODE::NETWORK_ERROR);
  _logger->info("stopped.");
}

void mesh_connection::stop_event_loop() {
  if (_evl_stoped) {
    return;
  }
  _evl_stoped = true;

  _io_context.stop();
  while (_threads_at_work.load() != 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  for (auto &&t : _threads) {
    t.join();
  }
  _threads.clear();
}

void mesh_connection::send_to(const node_name &from,
                              const node_name &to,
                              const append_entries &m) {
  _logger->dbg("send to ", to);
  queries::add_command_t cmd(from, m);
  auto messages = cmd.to_message();
  send_to(to, messages);
}

void mesh_connection::send_all(const node_name &from, const append_entries &m) {
  _logger->dbg("send all");
  auto all = all_nodes();
  for (auto &&o : std::move(all)) {
    if (o != _self_addr) {
      send_to(from, std::move(o), m);
    }
  }
}

void mesh_connection::send_all(const command_status_event_t &smuv) {
  _logger->dbg("send state_machine_updated_event_t to all");
  auto all = all_nodes();
  queries::clients::command_status_query_t rsmuq(smuv);
  auto m = rsmuq.to_message();
  for (auto &&to : std::move(all)) {
    if (to == _self_addr) {
      continue;
    }
    send_to(to, std::vector<dialler::message_ptr>{m});
  }
}

void mesh_connection::send_to(const node_name &to,
                              const std::vector<dialler::message_ptr> &m) {
  std::shared_lock l(_locker);
  if (auto it = _accepted_out_connections.find(to);
      it != _accepted_out_connections.end()) {
    if (auto dl_it = _diallers.find(it->second); dl_it != _diallers.end()) {
      dl_it->second->send_async(m);
    }
  }
}

size_t mesh_connection::size() {
  return all_nodes().size();
}

std::vector<node_name> mesh_connection::all_nodes() const {
  std::shared_lock l(_locker);
  std::vector<node_name> result;
  result.reserve(_accepted_input_connections.size() + 1);
  for (const auto &kv : _accepted_input_connections) {
    if (_accepted_out_connections.find(kv.second) != _accepted_out_connections.end()) {
      result.push_back(kv.second);
    }
  }
  result.push_back(_self_addr);
  return result;
}

void mesh_connection::accept_out_connection(const node_name &name,
                                            const std::string &addr) {
  bool call_client = false;
  {
    std::lock_guard l(_locker);
    ENSURE(_accepted_out_connections.find(name) == _accepted_out_connections.end());
    _logger->dbg("_accepted_out_connections: ", _accepted_out_connections.size());
    _accepted_out_connections.insert({name, addr});
    for (const auto &kv : _accepted_input_connections) {
      if (kv.second == name) {
        call_client = true;
        break;
      }
    }
  }
  if (call_client) {
    _client->new_connection_with(name);
  }
}

void mesh_connection::accept_input_connection(const node_name &name, uint64_t id) {
  bool call_client = false;
  {
    std::lock_guard l(_locker);
    ENSURE(_accepted_input_connections.find(id) == _accepted_input_connections.end());
    _logger->dbg("_accepted_input_connections: ", _accepted_input_connections.size());
    _accepted_input_connections.insert({id, name});
    call_client
        = (_accepted_out_connections.find(name) != _accepted_out_connections.end());
  }

  if (call_client) {
    _client->new_connection_with(name);
  }
}

node_name mesh_connection::addr_by_id(uint64_t id) {
  std::shared_lock l(_locker);
  if (auto it = _accepted_input_connections.find(id);
      it != _accepted_input_connections.end()) {
    return it->second;
  } else {
    THROW_EXCEPTION("id:", id, " not found");
  }
}

void mesh_connection::rm_out_connection(const std::string &addr,
                                        const boost::system::error_code &err) {
  node_name name;
  {
    std::lock_guard l(_locker);
    if (err != boost::asio::error::connection_reset) {
      _logger->dbg(addr, " disconnected as output. reason: ", err.message());
    } else {
      _logger->dbg(addr, " reconnection error");
    }
    for (auto it = _accepted_out_connections.begin();
         it != _accepted_out_connections.end();
         ++it) {
      if (it->second == addr) {
        name = it->first;
        _accepted_out_connections.erase(it);
        break;
      }
    }
  }
  if (!name.empty()) {
    {
      _client->lost_connection_with(name);
    }
    on_write_status(name, ERROR_CODE::NETWORK_ERROR);
  }
}

void mesh_connection::rm_input_connection(const uint64_t id,
                                          const boost::system::error_code &err) {
  try {
    auto name = addr_by_id(id);
    _logger->dbg(name, " disconnected as input. reason: ", err.message());
    {
      std::lock_guard l(_locker);
      _accepted_input_connections.erase(id);
    }
    on_write_status(name, ERROR_CODE::NETWORK_ERROR);
  } catch (...) {
  }
}

void mesh_connection::on_new_command(const std::vector<dialler::message_ptr> &m) {
  queries::add_command_t cmd_q(m);
  _logger->dbg("on_new_command: from=", cmd_q.from);
  _client->recv(cmd_q.from, cmd_q.cmd);
}

std::shared_ptr<async_result_t>
mesh_connection::send_to(const solidarity::node_name &target,
                         queries::resend_query_kind kind,
                         const solidarity::command_t &cmd,
                         std::function<void(ERROR_CODE)> callback) {
  std::lock_guard l(_locker);
  auto res = _ash.make_waiter();
  res->set_callback(callback);

  if (auto it = _accepted_out_connections.find(target);
      it != _accepted_out_connections.end()) {

    auto out_con = _diallers[it->second];
    _logger->dbg("send command to ", target);
    out_con->send_async(queries::resend_query_t(res->id(), kind, cmd).to_message());
  } else {
    res->set_result(cluster_state_event_t{},
                    ERROR_CODE::CONNECTION_NOT_FOUND,
                    "connection not found");
  }
  return res;
}

void mesh_connection::on_query_resend(const node_name &target,
                                      uint64_t mess_id,
                                      queries::resend_query_kind kind,
                                      solidarity::command_t &cmd) {
  std::vector<dialler::message_ptr> result;
  switch (kind) {
  case solidarity::queries::resend_query_kind::WRITE: {
    auto s = _client->add_command(cmd);
    auto m = s == ERROR_CODE::OK ? "" : to_string(s);
    result.push_back(queries::status_t(mess_id, s, m).to_message());
  } break;
  case solidarity::queries::resend_query_kind::STATUS: {
    auto s = _client->journal_state();
    auto l = _client->leader();
    result = queries::cluster_status_t(mess_id, l, s).to_message();
  } break;
  default:
    NOT_IMPLEMENTED;
  }

  std::lock_guard l(_locker);
  if (auto it = _accepted_out_connections.find(target);
      it != _accepted_out_connections.end()) {
    _diallers[it->second]->send_async(result);
  } else {
    _logger->warn("on_write_resend: connection ", target, " not found");
  }
}

void mesh_connection::on_write_status(uint64_t mess_id, ERROR_CODE status) {
  std::lock_guard l(_locker);
  auto w = _ash.get_waiter(mess_id);
  _ash.erase_waiter(mess_id);
  w->set_result(cluster_state_event_t(), status, "");
}

void mesh_connection::on_write_status(solidarity::node_name &target, ERROR_CODE status) {
  std::lock_guard l(_locker);

  auto results = _ash.get_waiter(target);
  for (auto &w : results) {
    w->set_result(cluster_state_event_t(), status, "");
  }
  _ash.erase_waiter(target);
}