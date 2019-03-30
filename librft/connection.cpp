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
    _parent->accept_out_connection(cluster_node().set_name(qc.node_id), _target_addr);
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
                                      const boost::system::error_code &err) {}

listener::listener(const std::shared_ptr<cluster_connection> parent) {
  _parent = parent;
}

void listener::on_network_error(dialler::listener_client_ptr i,
                                const dialler::message_ptr &d,
                                const boost::system::error_code &err) {}

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
      _parent->accept_input_connection(cluster_node().set_name(qc.node_id), i->get_id());
      _parent->_logger->info("accept connection from ", qc.node_id);
    }
    i->send_data(dout);
    break;
  }
  }
}

bool listener::on_new_connection(dialler::listener_client_ptr i) {
  return true;
}

void listener::on_disconnect(const dialler::listener_client_ptr &i) {}

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
  std::lock_guard<std::shared_mutex> l(_locker);
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
  std::lock_guard<std::shared_mutex> l(_locker);
  _logger->info(" stoping...");
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

  _stoped = true;

  _io_context.stop();

  for (auto &&t : _threads) {
    t.join();
  }
  _threads.clear();
  _logger->info(" stopped.");
}

void cluster_connection::send_to(const cluster_node &from,
                                 const cluster_node &to,
                                 const append_entries &m) {
  NOT_IMPLEMENTED;
}

void cluster_connection::send_all(const cluster_node &from, const append_entries &m) {
  NOT_IMPLEMENTED;
}

size_t cluster_connection::size() {
  std::shared_lock<std::shared_mutex> l(_locker);
  return _diallers.size();
}

std::vector<cluster_node> cluster_connection::all_nodes() const {
  std::shared_lock<std::shared_mutex> l(_locker);
  std::vector<cluster_node> result;
  result.reserve(_accepted_input_connections.size());
  for (const auto &kv : _accepted_input_connections) {
    result.push_back(kv.second);
  }
  return result;
}

void cluster_connection::accept_out_connection(const cluster_node &name,
                                               const cluster_node &addr) {
  std::lock_guard<std::shared_mutex> l(_locker);
  _accepted_out_connections.insert(std::make_pair(addr, name));
  _logger->dbg("_accepted_out_connections: ", _accepted_out_connections.size());
}

void cluster_connection::accept_input_connection(const cluster_node &name, uint64_t id) {
  std::lock_guard<std::shared_mutex> l(_locker);
  _accepted_input_connections.insert(std::make_pair(id, name));
  _logger->dbg("_accepted_input_connections: ", _accepted_input_connections.size());
}