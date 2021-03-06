#include <solidarity/dialler/listener.h>
#include <solidarity/mesh_connection.h>
#include <solidarity/node.h>
#include <solidarity/protocol_version.h>
#include <solidarity/queries.h>
#include <solidarity/raft.h>
#include <solidarity/utils/utils.h>

#include <boost/asio.hpp>

#include <future>
#include <list>

using namespace solidarity;
using namespace solidarity::queries;
using namespace solidarity::dialler;

class node_listener final : public abstract_listener_consumer {
public:
  node_listener(node *const parent, utils::logging::abstract_logger_ptr &l)
      : _parent(parent)
      , _logger(l) {}

  void on_new_message(listener_client_ptr i,
                      std::vector<dialler::message_ptr> &d,
                      bool &cancel) override {
    try {
      QUERY_KIND kind = static_cast<QUERY_KIND>(d.front()->get_header()->kind);
      switch (kind) {
      case QUERY_KIND::CONNECT: {
        connect_handler(i, d.front(), cancel);
        break;
      }
      case QUERY_KIND::READ: {
        read_handler(i, d);
        break;
      }
      case QUERY_KIND::WRITE: {
        write_handler(i, d);
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

  void connect_handler(listener_client_ptr i, message_ptr &d, bool &cancel) {
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
      connection_error_t ce(
          protocol_version, ERROR_CODE::WRONG_PROTOCOL_VERSION, "wrong protocol version");
      answer = ce.to_message();
    } else {
      query_connect_t q(protocol_version, _parent->params().name);
      answer = q.to_message();
      _client_name = cc.client_name;
      _parent->add_client(i->get_id());
      _logger->dbg("client:", _client_name, " accepted");
    }

    i->send_data(answer);
  }

  void read_handler(listener_client_ptr i, std::vector<message_ptr> &d) {
    clients::read_query_t rq(d);
    _logger->dbg("client:", _client_name, " read query #", rq.msg_id);
    command_t result = _parent->state_machine(rq.query.asm_num)->read(rq.query);
    clients::read_query_t answer(rq.msg_id, result);
    auto ames = answer.to_message();
    i->send_data(ames);
  }

  void write_handler(listener_client_ptr i, std::vector<message_ptr> &d) {
    clients::write_query_t wq(d);
    _logger->dbg("client:", _client_name, " write query #", wq.msg_id);
    auto res = _parent->add_command(wq.query);

    if (res == ERROR_CODE::NOT_A_LEADER) {
      _parent->send_to_leader(
          i->get_id(), queries::resend_query_kind::WRITE, wq.msg_id, wq.query);
    } else {
      queries::status_t st(wq.msg_id, res, std::string());
      i->send_data(st.to_message());
    }
  }

  bool on_new_connection(listener_client_ptr) override { return true; }

  void on_disconnect(const listener_client_ptr &i) override {
    _parent->rm_client(i->get_id());
  }

  void on_network_error(listener_client_ptr i,
                        const boost::system::error_code & /*err*/) override {
    _parent->rm_client(i->get_id());
  }

private:
  node *const _parent;
  utils::logging::abstract_logger_ptr _logger;
  std::string _client_name;
};

class consumer_wrapper final : public solidarity::abstract_state_machine {
public:
  consumer_wrapper(node *parent, solidarity::abstract_state_machine *t) {
    _target = t;
    _parent = parent;
  }

  void apply_cmd(const command_t &cmd) override {
    try {
      _target->apply_cmd(cmd);
      if (_parent->is_leader()) {
        _parent->notify_command_status(cmd.crc(), command_status::WAS_APPLIED);
      }
    } catch (...) {
      _parent->notify_command_status(cmd.crc(), command_status::APPLY_ERROR);
      throw;
    }
  }

  void reset() override {
    // TODO implement this
    //_parent->notify_state_machine_update();
    _target->reset();
  }

  command_t snapshot() override { return _target->snapshot(); }

  void install_snapshot(const solidarity::command_t &cmd) override {
    //_parent->notify_state_machine_update();
    _target->install_snapshot(cmd);
  }

  command_t read(const command_t &cmd) override { return _target->read(cmd); }

  bool can_apply(const command_t &cmd) override {
    bool res = _target->can_apply(cmd);
    if (res) {
      _parent->notify_command_status(cmd.crc(), command_status::CAN_BE_APPLY);
    } else {
      _parent->notify_command_status(cmd.crc(), command_status::CAN_NOT_BE_APPLY);
    }
    return res;
  }
  solidarity::abstract_state_machine *_target;
  node *_parent;
};

class journal_wrapper final : public logdb::abstract_journal {
public:
  journal_wrapper(logdb::journal_ptr target_, node *parent_) {
    _target = target_;
    _parent = parent_;
  }

  ~journal_wrapper() override {
    _target = nullptr;
    _parent = nullptr;
  }

  logdb::reccord_info put(const index_t idx, logdb::log_entry &e) override {
    if (_parent->is_leader()) {
      _parent->notify_command_status(e.cmd_crc, command_status::IN_LEADER_JOURNAL);
    }
    return _target->put(idx, e);
  }

  logdb::reccord_info put(const logdb::log_entry &e) override {
    if (_parent->is_leader()) {
      _parent->notify_command_status(e.cmd_crc, command_status::IN_LEADER_JOURNAL);
    }
    return _target->put(e);
  }

  void erase_all_after(const index_t lsn) override {
    std::list<uint32_t> erased;
    if (_parent->is_leader()) {
      _target->visit_after(
          lsn, [&erased](const logdb::log_entry &le) { erased.push_back(le.cmd_crc); });
    }
    _target->erase_all_after(lsn);

    if (_parent->is_leader()) {
      for (auto v : erased) {
        if (_parent->is_leader()) {
          // TODO implement batch version [{crc, kind}]
          _parent->notify_command_status(v, command_status::ERASED_FROM_JOURNAL);
        }
      }
    }
  }

  void erase_all_to(const index_t lsn) override {
    std::list<uint32_t> erased;
    if (_parent->is_leader()) {
      _target->visit_to(
          lsn, [&erased](const logdb::log_entry &le) { erased.push_back(le.cmd_crc); });
    }
    _target->erase_all_to(lsn);

    if (_parent->is_leader()) {
      for (auto v : erased) {
        if (_parent->is_leader()) {
          // TODO implement batch version [{crc, kind}]
          _parent->notify_command_status(v, command_status::ERASED_FROM_JOURNAL);
        }
      }
    }
  }

  void visit(std::function<void(const logdb::log_entry &)> v) override {
    return _target->visit(v);
  }

  void visit_after(const index_t lsn,
                   std::function<void(const logdb::log_entry &)> e) override {
    return _target->visit_after(lsn, e);
  }
  void visit_to(const index_t lsn,
                std::function<void(const logdb::log_entry &)> e) override {
    return _target->visit_after(lsn, e);
  }

  logdb::reccord_info prev_rec() const noexcept override { return _target->prev_rec(); }

  logdb::reccord_info first_uncommited_rec() const noexcept override {
    return _target->first_uncommited_rec();
  }
  logdb::reccord_info commited_rec() const noexcept override {
    return _target->commited_rec();
  }
  logdb::reccord_info first_rec() const noexcept override { return _target->first_rec(); }
  logdb::reccord_info restore_start_point() const noexcept override {
    return _target->restore_start_point();
  }
  logdb::reccord_info info(index_t lsn) const noexcept override {
    return _target->info(lsn);
  }

  virtual std::unordered_map<index_t, logdb::log_entry> dump() const override {
    return _target->dump();
  }

  void commit(const index_t lsn) override { return _target->commit(lsn); }
  logdb::log_entry get(const index_t lsn) override { return _target->get(lsn); }
  size_t size() const override { return _target->size(); }
  size_t reccords_count() const override { return _target->reccords_count(); }

  logdb::journal_ptr _target;
  node *_parent;
};

node::node(utils::logging::abstract_logger_ptr logger,
           const params_t &p,
           abstract_state_machine *state_machine) {
  std::unordered_map<uint32_t, abstract_state_machine *> sms
      = {{uint32_t(0), state_machine}};
  init(logger, p, sms);
}

node::node(utils::logging::abstract_logger_ptr logger,
           const params_t &p,
           std::unordered_map<uint32_t, abstract_state_machine *> state_machines) {
  init(logger, p, state_machines);
}

void node::init(utils::logging::abstract_logger_ptr logger,
                const params_t &p,
                std::unordered_map<uint32_t, abstract_state_machine *> state_machines) {
  _stoped = false;
  _params = p;

  for (auto &kv : state_machines) {
    _state_machine.insert({kv.first, new consumer_wrapper(this, kv.second)});
  }

  _logger = logger;

  auto jrn = solidarity::logdb::memory_journal::make_new();
  auto jrn_wrap = std::make_shared<journal_wrapper>(jrn, this);

  auto addr = _params.name;
  auto s = _params.raft_settings.set_name(_params.name);
  _raft
      = std::make_shared<solidarity::raft>(s, nullptr, jrn_wrap, _state_machine, _logger);

  solidarity::mesh_connection::params_t params;
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
      = std::make_shared<solidarity::mesh_connection>(addr, _raft, _logger, params);

  _cluster_con->set_state_machine_event_handler([this](const command_status_event_t &e) {
    this->notify_command_status(e.crc, e.status);
  });

  _raft->set_cluster(_cluster_con.get());

  dialler::listener::params_t lst_params;
  lst_params.port = _params.client_port;
  _listener = std::make_shared<dialler::listener>(_cluster_con->context(), lst_params);
  _listener_consumer = std::make_shared<node_listener>(this, _logger);
  _listener->add_consumer(_listener_consumer.get());

  auto original_period = _raft->settings().election_timeout().count();
  _timer_period = uint32_t(original_period * 0.1);

  _timer = std::make_unique<boost::asio::deadline_timer>(
      *_cluster_con->context(), boost::posix_time::milliseconds(_timer_period));
}

node::~node() {

  if (_cluster_con != nullptr) {
    stop();
  }
  _raft = nullptr;
  if (!_state_machine.empty()) {
    for (auto &&kv : std::move(_state_machine)) {
      delete kv.second;
    }

    _state_machine.clear();
  }
  _logger = nullptr;
  _clients.clear();
}

void node::start() {
  _stoped = false;

  _leader = _raft->get_leader();
  _kind = _raft->state().node_kind;

  _cluster_con->start();
  _timer->async_wait([this](auto) { this->heartbeat_timer(); });
  _listener->start();
  _listener->wait_starting();
}

void node::stop() {
  if (_stoped) {
    return;
  }

  {
    std::lock_guard l(_locker);
    if (_stoped) {
      return;
    }
    _stoped = true;
    _timer->cancel();

    if (_cluster_con != nullptr) {
      _cluster_con->stop_event_loop();
    }

    _timer = nullptr;
  }

  if (_listener != nullptr) {
    _listener->stop();
    _listener->wait_stoping();
  }

  if (_cluster_con != nullptr) {
    _cluster_con->stop();
    _cluster_con = nullptr;
  }

  if (_listener != nullptr) {
    _listener = nullptr;
    _listener_consumer = nullptr;
  }
}

bool node::is_leader() const {
  std::shared_lock l(_state_locker);
  return _kind == NODE_KIND::LEADER;
}

raft_state_t node::state() const {
  return _raft->state();
}

node_name node::self_name() const {
  return _raft->self_addr();
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

void node::notify_command_status(uint32_t crc, command_status k) {
  if (_stoped) {
    return;
  }
  _logger->dbg("notify_command_status() ", crc, k);

  bool is_leader_flag = is_leader();
  std::shared_lock l(_locker);
  std::future<void> a;
  command_status_event_t smev;
  smev.crc = crc;
  smev.status = k;

  if (!_on_update_handlers.empty()) {
    a = std::async(std::launch::async, [this, smev]() {
      client_event_t ev;
      ev.kind = client_event_t::event_kind::COMMAND_STATUS;
      ev.cmd_ev = smev;
      for (auto &v : _on_update_handlers) {
        v.second(ev);
      }
    });
  }

  auto context = _cluster_con->context();
  for (auto v : _clients) {
    auto m = clients::command_status_query_t(smev).to_message();
    boost::asio::post(*context, [this, m, v]() { this->_listener->send_to(v, m); });
  }

  if (is_leader_flag) {
    _cluster_con->send_all(smev);
  }

  if (a.valid()) {
    a.wait();
  }
}

void node::notify_raft_state_update(NODE_KIND old_state, NODE_KIND new_state) {
  if (_stoped) {
    return;
  }
  _logger->dbg("notify_raft_state_update(): ", old_state, " => ", new_state);

  std::unordered_map<uint64_t, std::function<void(const client_event_t &)>> handlers_cp;
  std::unordered_set<uint64_t> clients_cp;
  {
    std::shared_lock l(_locker);
    handlers_cp = _on_update_handlers;
    clients_cp = _clients;
  }

  if (!handlers_cp.empty() && !_stoped) {
    client_event_t ev;
    ev.kind = client_event_t::event_kind::RAFT;
    ev.raft_ev = raft_state_event_t{old_state, new_state};

    for (auto &v : handlers_cp) {
      v.second(ev);
    }
  }

  if (!clients_cp.empty()) {
    clients::raft_state_updated_t rsu(old_state, new_state);
    auto m = rsu.to_message();
    for (auto v : clients_cp) {
      this->_listener->send_to(v, m);
    }
  }
}

abstract_state_machine *node::state_machine(uint32_t asm_number) {
  return dynamic_cast<consumer_wrapper *>(_state_machine[asm_number])->_target;
}

std::shared_ptr<raft> node::get_raft() {
  return _raft;
}

uint64_t node::add_event_handler(const std::function<void(const client_event_t &)> &h) {
  auto id = _next_id.fetch_add(1);
  std::lock_guard l(_locker);
  _on_update_handlers[id] = h;
  return id;
}

void node::rm_event_handler(uint64_t id) {
  std::lock_guard l(_locker);
  if (auto it = _on_update_handlers.find(id); it != _on_update_handlers.end()) {
    _on_update_handlers.erase(it);
  }
}

void node::heartbeat_timer() {
  auto exists_size = _cluster_con->size();
  auto max_size = _params.cluster.size() + 1;
  auto qr = quorum_for_cluster(max_size, _params.raft_settings.vote_quorum());
  if (exists_size >= qr) {
    _raft->heartbeat();
    auto leader = _raft->get_leader();
    auto kind = _raft->state().node_kind;
    std::lock_guard l(_state_locker);
    if (leader != _leader || kind != _kind) {
      notify_raft_state_update(_kind, kind);
      _leader = leader;
      _kind = kind;
    }
  } else {
    _logger->warn("visible cluster is to small for quorum. exists: ",
                  exists_size,
                  " max_size:",
                  max_size,
                  " quorum: ",
                  qr);
  }
  if (!_stoped) {
    std::lock_guard l(_locker);
    _timer->expires_at(_timer->expires_at()
                       + boost::posix_time::milliseconds(_timer_period));
    _timer->async_wait([this](auto) { this->heartbeat_timer(); });
  }
}

void node::send_to_leader(uint64_t client_id,
                          queries::resend_query_kind kind,
                          uint64_t message_id,
                          command_t &cmd) {
  if (_stoped) {
    return;
  }

  auto leader = _raft->state().leader;
  if (leader.empty()) {
    queries::status_t s(message_id, solidarity::ERROR_CODE::UNDER_ELECTION, _params.name);
    auto answer = s.to_message();
    _listener->send_to(client_id, answer);
  } else {
    _logger->dbg("resend query #", client_id, " to leader ", leader);
    {
      std::lock_guard l(_locker);
      _message_resend[client_id].push_back(std::pair(message_id, cmd));
    }
    _cluster_con->send_to(leader, kind, cmd, [client_id, message_id, this](ERROR_CODE s) {
      this->on_message_sended_status(client_id, message_id, s);
    });
  }
}

void node::on_message_sended_status(uint64_t client,
                                    uint64_t message,
                                    ERROR_CODE status) {
  _logger->dbg(
      "on_message_sended_status client:", client, " #", message, " status:", status);
  if (_stoped) {
    return;
  }
  dialler::message_ptr answer = nullptr;
  {
    std::lock_guard l(_locker);
    // TODO refact

    auto pos = std::find_if(_message_resend[client].begin(),
                            _message_resend[client].end(),
                            [message](auto p) { return p.first = message; });
    if (pos != _message_resend[client].end()) {
      status_t s(message, status, std::string());
      answer = s.to_message();
      _message_resend[client].erase(pos);
    }
  }
  if (answer != nullptr) {
    _listener->send_to(client, answer);
  }
}

ERROR_CODE node::add_command(const command_t &cmd) {
  if (_stoped) {
    return ERROR_CODE::NETWORK_ERROR;
  }
  auto nk = state().node_kind;
  if (nk != NODE_KIND::LEADER && nk != NODE_KIND::FOLLOWER) {
    return ERROR_CODE::UNDER_ELECTION;
  } else {
    if (nk == NODE_KIND::LEADER) {
      auto ec = get_raft()->add_command(cmd);
      return ec;
    }
    return ERROR_CODE::NOT_A_LEADER;
  }
}

std::shared_ptr<async_result_t> node::add_command_to_cluster(const command_t &cmd) {
  if (_stoped) {
    return nullptr;
  }
  _logger->dbg("add_command_to_cluster");
  auto result = std::make_shared<async_result_t>(uint64_t(0));
  auto st = add_command(cmd);
  if (st == ERROR_CODE::OK) {
    result->set_result(std::vector<uint8_t>{}, st, "");
    return result;
  }
  auto rft_st = _raft->state();
  auto leader = rft_st.leader;
  if (leader.empty() || rft_st.node_kind == NODE_KIND::CANDIDATE
      || rft_st.node_kind == NODE_KIND::ELECTION) {
    result->set_result(
        std::vector<uint8_t>{}, solidarity::ERROR_CODE::UNDER_ELECTION, "");
    return result;
  }

  auto callback = [result](ERROR_CODE s) {
    result->set_result(
        std::vector<uint8_t>{}, s, s == ERROR_CODE::OK ? "" : to_string(s));
  };
  _cluster_con->send_to(leader, queries::resend_query_kind::WRITE, cmd, callback);
  return result;
}

cluster_state_event_t node::cluster_status() {
  if (_stoped) {
    return cluster_state_event_t();
  }

  if (is_leader()) {
    cluster_state_event_t cse;

    auto jstate = _raft->journal_state();
    {
      std::lock_guard l(_state_locker);
      cse.leader = _leader;
    }
    for (auto &&kv : std::move(jstate)) {
      cse.state.insert(std::pair(kv.first, kv.second));
    }
    return cse;
  } else {
    auto leader = _raft->get_leader();
    auto ar = _cluster_con->send_to(leader,
                                    queries::resend_query_kind::STATUS,
                                    solidarity::command_t(),
                                    [](ERROR_CODE) {});
    return ar->cluster_state();
  }
}