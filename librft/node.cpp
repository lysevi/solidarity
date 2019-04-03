#include <librft/connection.h>
#include <librft/consensus.h>
#include <librft/node.h>
#include <libdialler/listener.h>
#include <boost/asio.hpp>

using namespace rft;

class node_listener : public dialler::abstract_listener_consumer {
public:
  node_listener(node *const parent)
      : _parent(parent) {}

  void on_network_error(dialler::listener_client_ptr i,
                        const dialler::message_ptr &d,
                        const boost::system::error_code &err) override {}

  void on_new_message(dialler::listener_client_ptr i,
                      dialler::message_ptr &&d,
                      bool &cancel) override {}

  bool on_new_connection(dialler::listener_client_ptr i) override { return true; }

  void on_disconnect(const dialler::listener_client_ptr &i) override {}

private:
  node *const _parent;
};

node::node(const params_t &p, abstract_consensus_consumer *consumer) {
  _params = p;
  _consumer = consumer;
  auto log_prefix = utils::strings::args_to_string(p.name, "> ");
  _logger = std::make_shared<utils::logging::prefix_logger>(
      utils::logging::logger_manager::instance()->get_logger(), log_prefix);

  auto jrn = std::make_shared<rft::logdb::memory_journal>();
  auto addr = rft::cluster_node().set_name(_params.name);
  auto s = rft::node_settings().set_name(_params.name);
  _consensus = std::make_shared<rft::consensus>(s, nullptr, jrn, consumer);

  rft::cluster_connection::params_t params;
  params.listener_params.port = p.port;
  params.thread_count = p.thread_count;
  params.addrs.reserve(p.cluster.size());
  std::transform(p.cluster.begin(),
                 p.cluster.end(),
                 std::back_inserter(params.addrs),
                 [](const auto addr) -> dialler::dial::params_t {
                   auto splitted = utils::strings::split(addr, ':');
                   dialler::dial::params_t result(
                       splitted[0], (unsigned short)std::stoi(splitted[1]), true);
                   return result;
                 });

  _cluster_con
      = std::make_shared<rft::cluster_connection>(addr, _consensus, _logger, params);
  _consensus->set_cluster(_cluster_con.get());

  dialler::listener::params_t lst_params;
  lst_params.port = _params.client_port;
  _listener = std::make_shared<dialler::listener>(_cluster_con->context(), lst_params);
  _listener_consumer = std::make_shared<node_listener>(this);
  _listener->add_consumer(_listener_consumer.get());
}

node::~node() {
  if (_cluster_con != nullptr) {
    stop();
  }
}

void node::start() {
  _cluster_con->start();
  _listener->start();
  _listener->wait_starting();
}

void node::stop() {
  if (_listener != nullptr) {
    _listener->stop();
    _listener->wait_stoping();
    _listener = nullptr;
  }

  if (_cluster_con != nullptr) {
    _cluster_con->stop();
    _cluster_con = nullptr;
  }
}

node_state_t node::state() const {
  return _consensus->state();
}

cluster_node node::self_name() const {
  return _consensus->self_addr();
}
