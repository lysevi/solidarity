#include "test_description_t.h"

void test_description_t::init(size_t cluster_size, bool add_listener_handler) {
  std::vector<unsigned short> ports(cluster_size);
  std::iota(ports.begin(), ports.end(), (unsigned short)(8000));

  std::cerr << "start nodes" << std::endl;
  unsigned short client_port = 10000;
  for (auto p : ports) {
    std::vector<unsigned short> out_ports;
    out_ports.reserve(ports.size() - 1);
    std::copy_if(ports.begin(),
                 ports.end(),
                 std::back_inserter(out_ports),
                 [p](const auto v) { return v != p; });

    EXPECT_EQ(out_ports.size(), ports.size() - 1);

    std::vector<std::string> out_addrs;
    out_addrs.reserve(out_ports.size());
    std::transform(out_ports.begin(),
                   out_ports.end(),
                   std::back_inserter(out_addrs),
                   [](const auto prt) {
                     return solidarity::utils::strings::to_string("localhost:", prt);
                   });

    solidarity::node::params_t params;
    params.port = p;
    params.client_port = client_port++;
    params.thread_count = 1;
    params.cluster = out_addrs;
    params.name = solidarity::utils::strings::to_string("node_", p);
    std::cerr << params.name << " starting..." << std::endl;
    auto log_prefix = solidarity::utils::strings::to_string(params.name, "> ");
    auto node_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
        solidarity::utils::logging::logger_manager::instance()->get_shared_logger(),
        log_prefix);

    auto state_machines = get_state_machines();
    std::unordered_map<uint32_t, solidarity::abstract_state_machine *> for_ctor(
        state_machines.size());
    for (auto &sms_kv : state_machines) {
      for_ctor.insert({sms_kv.first, sms_kv.second.get()});
    }
    auto n = std::make_shared<solidarity::node>(node_logger, params, for_ctor);

    n->start();
    if (add_listener_handler) {
      n->add_event_handler([](const solidarity::client_event_t &ev) {
        if (ev.kind == solidarity::client_event_t::event_kind::COMMAND_STATUS) {
          std::stringstream ss;
          ss << "command " << solidarity::to_string(ev);
          std::cerr << ss.str() << std::endl;
        }
      });
    }

    solidarity::client::params_t cpar(
        solidarity::utils::strings::to_string("client_", params.name));
    cpar.threads_count = 1;
    cpar.host = "localhost";
    cpar.port = params.client_port;

    auto c = std::make_shared<solidarity::client>(cpar);
    c->connect();

    while (!c->is_connected()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_EQ(n->connections_count(), size_t(1));

    consumers[params.name] = state_machines;
    nodes[params.name] = n;
    clients[params.name] = c;
  }
}

std::unordered_set<solidarity::node_name> test_description_t::wait_election() {
  std::cerr << "wait election" << std::endl;
  std::unordered_set<solidarity::node_name> leaders;
  while (true) {
    leaders.clear();
    for (auto &kv : nodes) {
      if (kv.second->state().node_kind == solidarity::NODE_KIND::LEADER) {
        leaders.insert(kv.second->self_name());
      }
    }
    if (leaders.size() == 1) {
      auto leader_name = *leaders.begin();
      bool election_complete = true;
      for (auto &kv : nodes) {
        auto state = kv.second->state();
        auto nkind = state.node_kind;
        if (nkind != solidarity::NODE_KIND::LEADER
            && nkind != solidarity::NODE_KIND::FOLLOWER) {
          election_complete = false;
          break;
        }
        if ((nkind == solidarity::NODE_KIND::LEADER
             || nkind == solidarity::NODE_KIND::FOLLOWER)
            && state.leader != leader_name) {
          election_complete = false;
          break;
        }
      }
      if (election_complete) {
        break;
      }
    }
  }
  return leaders;
}