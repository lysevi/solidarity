#include <librft/connection.h>
#include <librft/queries.h>
#include <libutils/utils.h>

using namespace rft;

namespace rft::impl {
out_connection::out_connection(const std::shared_ptr<cluster_connection> parent,
                               const cluster_node &target_addr) {
  _parent = parent;
  _target_addr = target_addr;
}

void out_connection::on_connect() {
  _parent->_logger->info("connect to ", _target_addr);
  this->_connection->send_async(
      queries::query_connect_t(protocol_version, _parent->_self_addr.name())
          .to_message());
}

void out_connection::on_new_message(dialler::message_ptr &&d, bool &cancel) {
  using namespace queries;
  QUERY_KIND kind = static_cast<QUERY_KIND>(d->get_header()->kind);
  switch (kind) {
  case QUERY_KIND::CONNECT: {
    query_connect_t qc(d);
    _self_logical_addr.set_name(qc.node_id);
    _parent->accept_out_connection(_self_logical_addr, _target_addr);
    break;
  }
  case QUERY_KIND::CONNECTION_ERROR: {
    // TODO erase connection;
    cancel;
    break;
  }
  }
}

void out_connection::on_network_error(const dialler::message_ptr &d,
                                      const boost::system::error_code &err) {
  _parent->rm_out_connection(_self_logical_addr);
}

listener::listener(const std::shared_ptr<cluster_connection> parent) {
  _parent = parent;
}

void listener::on_network_error(dialler::listener_client_ptr i,
                                const dialler::message_ptr &d,
                                const boost::system::error_code &err) {
  ENSURE(!_self_logical_addr.name().empty());
  _parent->rm_input_connection(_self_logical_addr);
}

void listener::on_new_message(dialler::listener_client_ptr i,
                              dialler::message_ptr &&d,
                              bool &cancel) {
  using namespace queries;
  QUERY_KIND kind = static_cast<QUERY_KIND>(d->get_header()->kind);
  switch (kind) {
  case QUERY_KIND::CONNECT: {
    query_connect_t qc(d);
    dialler::message_ptr dout;
    if (qc.protocol_version != protocol_version) {
      dout = connection_error_t(protocol_version, "protocol version error").to_message();
    } else {
      dout = query_connect_t(protocol_version, _parent->_self_addr.name()).to_message();
      _self_logical_addr.set_name(qc.node_id);
      ENSURE(!_self_logical_addr.name().empty());
      _parent->accept_input_connection(_self_logical_addr, i->get_id());
      _parent->_logger->info("accept connection from ", _self_logical_addr);
    }
    i->send_data(dout);
    break;
  }
  case QUERY_KIND::COMMAND: {
    if (d->get_header()->is_single_message()) {
      std::vector<dialler::message_ptr> m({d});
      _parent->on_new_command(m);
    } else {
      _recv_message_pool.push_back(d);
      if (d->get_header()->is_end_block) {
        _parent->on_new_command(std::move(_recv_message_pool));
        _recv_message_pool.clear();
      }
    }
    break;
  }
  }
}

bool listener::on_new_connection(dialler::listener_client_ptr i) {
  return true;
}

void listener::on_disconnect(const dialler::listener_client_ptr &i) {
  _parent->rm_input_connection(_self_logical_addr);
}

} // namespace rft::impl

cluster_connection::cluster_connection(
    cluster_node self_addr,
    const std::shared_ptr<abstract_cluster_client> &client,
    const utils::logging::abstract_logger_ptr &logger,
    const cluster_connection::params_t &params) {
  _logger = logger;
  _client = client;
  _params = params;
  _self_addr = self_addr;
}

cluster_connection::~cluster_connection() {
  stop();
}

void cluster_connection::start() {
  std::lock_guard l(_locker);
  _stoped = false;
  _threads.resize(_params.thread_count);
  for (size_t i = 0; i < _params.thread_count; ++i) {
    _threads[i] = std::thread([this]() {
      while (!_stoped) {
        _io_context.run();
      }
    });
  }
  auto self = shared_from_this();
  _listener_consumer = std::make_shared<impl::listener>(self);
  _listener = std::make_unique<dialler::listener>(&_io_context, _params.listener_params);
  _listener->add_consumer(_listener_consumer.get());
  _listener->start();
  _listener->wait_starting();

  for (auto &p : _params.addrs) {

    auto cnaddr = cluster_node().set_name(p.host + ":" + std::to_string(p.port));
    auto c = std::make_shared<impl::out_connection>(self, cnaddr);

    ENSURE(_diallers.find(cnaddr) == _diallers.end());
    ENSURE(!cnaddr.is_empty());

    auto d = std::make_shared<dialler::dial>(&_io_context, p);
    d->add_consumer(c);
    _diallers.insert(std::make_pair(cnaddr, d));
    d->start_async_connection();
  }
}

void cluster_connection::stop() {

  _logger->info(" stoping...");
  _stoped = true;

  _io_context.stop();

  for (auto &&t : _threads) {
    t.join();
  }
  _threads.clear();

  std::vector<std::shared_ptr<dialler::dial>> diallers_to_stop;
  {
    std::shared_lock l(_locker);
    diallers_to_stop.reserve(_diallers.size());
    for (auto &&kv : _diallers) {
      diallers_to_stop.push_back(kv.second);
      kv.second->disconnect();
      kv.second->wait_stoping();
    }
  }

  for (auto &&v : diallers_to_stop) {
    v->disconnect();
    v->wait_stoping();
  }

  {
    std::lock_guard l(_locker);
    _diallers.clear();
  }

  if (_listener != nullptr) {
    _listener->stop();
    _listener->wait_stoping();
    _listener = nullptr;
    _listener_consumer = nullptr;
  }

  _logger->info(" stopped.");
}

void cluster_connection::send_to(const cluster_node &from,
                                 const cluster_node &to,
                                 const append_entries &m) {
  std::shared_lock l(_locker);
  _logger->dbg("send to ", to);
  if (auto it = _accepted_out_connections.find(to);
      it != _accepted_out_connections.end()) {
    queries::command_t cmd(from, m);
    if (auto dl_it = _diallers.find(it->second); dl_it != _diallers.end()) {
      auto messages = cmd.to_message();
      if (messages.size() == 1) {
        dl_it->second->send_async(messages.front());
      } else {
        dl_it->second->send_async(messages);
      }
    }
  }
}

void cluster_connection::send_all(const cluster_node &from, const append_entries &m) {
  _logger->dbg("send all");
  auto all = all_nodes();
  for (auto &&o : std::move(all)) {
    send_to(from, std::move(o), m);
  }
}

size_t cluster_connection::size() {
  return all_nodes().size();
}

std::vector<cluster_node> cluster_connection::all_nodes() const {
  std::shared_lock l(_locker);
  std::vector<cluster_node> result;
  result.reserve(_accepted_input_connections.size());
  for (const auto &kv : _accepted_input_connections) {
    if (_accepted_out_connections.find(kv.first) != _accepted_out_connections.end()) {
      result.push_back(kv.first);
    }
  }
  return result;
}

void cluster_connection::accept_out_connection(const cluster_node &name,
                                               const cluster_node &addr) {
  std::lock_guard l(_locker);
  _accepted_out_connections.insert({name, addr});
  _logger->dbg("_accepted_out_connections: ", _accepted_out_connections.size());
}

void cluster_connection::accept_input_connection(const cluster_node &name, uint64_t id) {
  std::lock_guard l(_locker);
  _accepted_input_connections.insert({name, id});
  _logger->dbg("_accepted_input_connections: ", _accepted_input_connections.size());
}

void cluster_connection::rm_out_connection(const cluster_node &name) {
  std::lock_guard l(_locker);
  _logger->dbg(name, " disconnected as output");
  if (auto it = _accepted_out_connections.find(name);
      it != _accepted_out_connections.end()) {
    _accepted_out_connections.erase(it);
    _client->lost_connection_with(name);
  }
}

void cluster_connection::rm_input_connection(const cluster_node &name) {
  std::lock_guard l(_locker);
  _logger->dbg(name, " disconnected as input");
  if (auto it = _accepted_input_connections.find(name);
      it != _accepted_input_connections.end()) {
    _accepted_input_connections.erase(it);
    _client->lost_connection_with(name);
  }
}

void cluster_connection::on_new_command(const std::vector<dialler::message_ptr> &m) {
  std::shared_lock l(_locker);
  queries::command_t cmd_q(m);
  _logger->dbg("on_new_command: from=", cmd_q.from);
  _client->recv(cmd_q.from, cmd_q.cmd);
}