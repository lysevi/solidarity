#include <librft/consensus.h>
#include <librft/mesh_connection.h>
#include <librft/node.h>
#include <librft/protocol_version.h>
#include <librft/queries.h>
#include <libdialler/listener.h>
#include <libutils/utils.h>

#include <boost/asio.hpp>

using namespace rft;
using namespace rft::queries;
using namespace dialler;

class node_listener : public dialler::abstract_listener_consumer {
public:
  node_listener(node *const parent, utils::logging::abstract_logger_ptr &l)
      : _parent(parent)
      , _logger(l) {}

  void on_new_message(dialler::listener_client_ptr i,
                      dialler::message_ptr &&d,
                      bool &cancel) override {
    try {
      QUERY_KIND kind = static_cast<QUERY_KIND>(d->get_header()->kind);
      switch (kind) {
      case QUERY_KIND::CONNECT: {
        connect_handler(i, std::move(d), cancel);
        break;
      }
      case QUERY_KIND::READ: {
        read_handler(i, std::move(d));
        break;
      }
      case QUERY_KIND::WRITE: {
        write_handler(i, std::move(d));
        break;
      }
      default:
        NOT_IMPLEMENTED;
      }
    } catch (utils::exceptions::exception_t &e) {
      _logger->fatal(e.what());
    } catch (std::exception &e) {
      _logger->fatal(e.what());
    }
  }

  void connect_handler(listener_client_ptr i, message_ptr &&d, bool &cancel) {
    clients::client_connect_t cc(d);
    message_ptr answer = nullptr;
    if (cc.protocol_version != protocol_version) {
      _logger->fatal("client: ",
                     cc.client_name,
                     "wrong protocol version: get:",
                     cc.protocol_version,
                     " expected:",
                     protocol_version);
      cancel = true;
      connection_error_t ce(protocol_version, "wrong protocol version");
      answer = ce.to_message();
    } else {
      query_connect_t q(protocol_version, _parent->params().name, "");
      answer = q.to_message();
      _client_name = cc.client_name;
      _parent->add_client(i->get_id());
    }

    i->send_data(answer);
  }

  void read_handler(listener_client_ptr i, message_ptr &&d) {
    clients::read_query_t rq(d);
    _logger->dbg("client:", _client_name, " read query #", rq.msg_id);
    command result = _parent->consumer()->read(rq.query);
    clients::read_query_t answer(rq.msg_id, result);
    auto ames = answer.to_message();
    i->send_data(ames);
  }

  void write_handler(listener_client_ptr i, message_ptr &&d) {
    clients::write_query_t wq(d);
    _logger->dbg("client:", _client_name, " write query #", wq.msg_id);
    bool writed = false;
    if (_parent->state().node_kind == NODE_KIND::LEADER) {
      _parent->get_consensus()->add_command(wq.query);
      if (_parent->state().node_kind != NODE_KIND::LEADER) {
        writed = false;
      } else {
        status_t s(wq.msg_id, ERROR_CODE::OK, std::string());
        auto ames = s.to_message();
        i->send_data(ames);
        writed = true;
      }
    }

    if (!writed) {
      _parent->send_to_leader(i->get_id(), wq.msg_id, wq.query);
    }
  }

  bool on_new_connection(dialler::listener_client_ptr) override { return true; }

  void on_disconnect(const dialler::listener_client_ptr &i) override {
    _parent->rm_client(i->get_id());
  }

  void on_network_error(listener_client_ptr i,
                        const message_ptr &d,
                        const boost::system::error_code &err) override {
    _parent->rm_client(i->get_id());
  }

private:
  node *const _parent;
  utils::logging::abstract_logger_ptr _logger;
  std::string _client_name;
};

class consumer_wrapper : public rft::abstract_consensus_consumer {
public:
  consumer_wrapper(node *parent, rft::abstract_consensus_consumer *t) {
    _target = t;
    _parent = parent;
  }

  void apply_cmd(const command &cmd) override {
    _parent->notify_state_machine_update();
    _target->apply_cmd(cmd);
  }

  void reset() override {
    _parent->notify_state_machine_update();
    _target->reset();
  }

  command snapshot() override { return _target->snapshot(); }

  command read(const command &cmd) override { return _target->read(cmd); }

  rft::abstract_consensus_consumer *_target;
  node *_parent;
};

node::node(utils::logging::abstract_logger_ptr logger,
           const params_t &p,
           abstract_consensus_consumer *consumer) {
  _params = p;
  _consumer = new consumer_wrapper(this, consumer);

  _logger = logger;

  auto jrn = std::make_shared<rft::logdb::memory_journal>();
  auto addr = rft::cluster_node().set_name(_params.name);
  auto s = rft::node_settings().set_name(_params.name);
  _consensus = std::make_shared<rft::consensus>(s, nullptr, jrn, _consumer, _logger);

  rft::mesh_connection::params_t params;
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
      = std::make_shared<rft::mesh_connection>(addr, _consensus, _logger, params);
  _consensus->set_cluster(_cluster_con.get());

  dialler::listener::params_t lst_params;
  lst_params.port = _params.client_port;
  _listener = std::make_shared<dialler::listener>(_cluster_con->context(), lst_params);
  _listener_consumer = std::make_shared<node_listener>(this, _logger);
  _listener->add_consumer(_listener_consumer.get());

  auto original_period = _consensus->settings().election_timeout().count();
  _timer_period = uint32_t(original_period * 0.1);
  _timer = std::make_unique<boost::asio::deadline_timer>(
      *_cluster_con->context(), boost::posix_time::milliseconds(_timer_period));
}

node::~node() {
  if (_cluster_con != nullptr) {
    stop();
  }
  if (_consumer != nullptr) {
    delete _consumer;
    _consumer = nullptr;
  }
}

void node::start() {
  _stoped = false;
  _cluster_con->start();
  _timer->async_wait([this](auto) { this->heartbeat_timer(); });
  _listener->start();
  _listener->wait_starting();
}

void node::stop() {
  if (_listener != nullptr) {
    _stoped = true;
    _timer->cancel();

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

void node::add_client(uint64_t id) {
  std::lock_guard l(_locker);
  _clients.insert(id);
}

void node::rm_client(uint64_t id) {
  std::lock_guard l(_locker);
  _clients.erase(id);
}

size_t node::connections_count() const {
  std::shared_lock l(_locker);
  return _clients.size();
}

void node::notify_state_machine_update() {
  _logger->dbg("notify_state_machine_update()");
  std::shared_lock l(_locker);
  for (auto v : _clients) {
    auto m = clients::state_machine_updated_t().to_message();
    this->_listener->send_to(v, m);
  }
}

abstract_consensus_consumer *node::consumer() {
  return dynamic_cast<consumer_wrapper *>(_consumer)->_target;
}

std::shared_ptr<consensus> node::get_consensus() {
  return _consensus;
}

void node::heartbeat_timer() {
  _consensus->heartbeat();
  if (!_stoped) {
    _timer->expires_at(_timer->expires_at()
                       + boost::posix_time::milliseconds(_timer_period));
    _timer->async_wait([this](auto) { this->heartbeat_timer(); });
  }
}

void node::send_to_leader(uint64_t client_id, uint64_t message_id, command &cmd) {
  _logger->dbg("resend query #", client_id, " to leader");
  std::lock_guard l(_locker);
  _message_resend[client_id].push_back(std::pair(message_id, cmd));

  _cluster_con->send_to(
      _consensus->state().leader, cmd, [client_id, message_id, this](ERROR_CODE s) {
        // TODO use shared_from_this;
        this->on_message_sended_status(client_id, message_id, s);
      });
}

void node::on_message_sended_status(uint64_t client,
                                    uint64_t message,
                                    ERROR_CODE status) {
  _logger->dbg(
      "on_message_sended_status client:", client, " #", message, " status:", status);
  std::lock_guard l(_locker);
  // TODO if!is_ok?
  // TODO refact
  auto pos = std::find_if(_message_resend[client].begin(),
                          _message_resend[client].end(),
                          [message](auto p) { return p.first = message; });
  if (pos != _message_resend[client].end()) {
    status_t s(message, status, std::string());
    _listener->send_to(client, s.to_message());
    _message_resend[client].erase(pos);
  }
}