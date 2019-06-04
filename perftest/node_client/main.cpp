#include <iostream>
#include <numeric>

#include <solidarity/solidarity.h>
#include <solidarity/utils/utils.h>

#include <cxxopts.hpp>

using solidarity::utils::strings::to_string;

solidarity::utils::logging::abstract_logger_ptr logger_ptr = nullptr;

class dummy_fsm final : public solidarity::abstract_state_machine {
public:
  dummy_fsm()
      : counter(0) {}

  void apply_cmd(const solidarity::command_t &cmd) override {
    counter = cmd.to_value<uint64_t>();
  }

  void reset() override { counter = 0; }

  solidarity::command_t snapshot() override {
    return solidarity::command_t::from_value<uint64_t>(counter);
  }

  void install_snapshot(const solidarity::command_t &cmd) override {
    counter = cmd.to_value<uint64_t>();
  }

  solidarity::command_t read(const solidarity::command_t & /*cmd*/) override {
    return snapshot();
  }

  bool can_apply(const solidarity::command_t &) override { return true; }

  uint64_t counter;
};

size_t thread_per_node_count = 1;
unsigned short start_port = 10000;
unsigned short start_c_port = 11000;
size_t node_count = 1;
size_t writes_count = 1000;
bool sync_writes = true;
bool verbose = false;

int main(int argc, char **argv) {
  cxxopts::Options options("Distributed increment", "Example distributed increment");
  options.allow_unrecognised_options();
  options.positional_help("[optional args]").show_positional_help();

  auto add_o = options.add_options();
  add_o("v,verbose", "Enable debugging");
  add_o("h,help", "Help");
  add_o("async", "async writer");
  add_o("t,threads",
        "Threads for each node.",
        cxxopts::value<size_t>(thread_per_node_count));
  add_o("n,nodes", "Nodes count in a cluster.", cxxopts::value<size_t>(node_count));
  add_o("w,writes_count", "writes_count.", cxxopts::value<size_t>(writes_count));

  try {
    cxxopts::ParseResult result = options.parse(argc, argv);

    if (result["help"].as<bool>()) {
      std::cout << options.help() << std::endl;
      std::exit(0);
    }
    if (result["async"].as<bool>()) {
      sync_writes = false;
    }

    if (result["verbose"].as<bool>()) {
      verbose = true;

    } else {
      verbose = false;
    }

    logger_ptr = std::make_shared<solidarity::utils::logging::file_logger>(
        "bench_client_node", verbose);

  } catch (cxxopts::OptionException &ex) {
    logger_ptr->fatal(ex.what());
  }

  logger_ptr->info("thread_per_node_count: ", thread_per_node_count);
  logger_ptr->info("start_port: ", start_port);
  logger_ptr->info("start_c_port: ", start_c_port);
  logger_ptr->info("node_count: ", node_count);
  logger_ptr->info("sync_writes: ", sync_writes);

  std::vector<std::shared_ptr<solidarity::node>> nodes(node_count);
  std::vector<std::shared_ptr<solidarity::client>> clients(node_count);
  std::vector<std::shared_ptr<dummy_fsm>> smachines(node_count);

  std::vector<unsigned short> ports(node_count);
  std::iota(ports.begin(), ports.end(), (unsigned short)(start_port));

  for (size_t i = 0; i < node_count; ++i) {
    std::vector<unsigned short> out_ports;
    out_ports.reserve(ports.size() - 1);
    std::copy_if(ports.begin(),
                 ports.end(),
                 std::back_inserter(out_ports),
                 [i, &ports](const auto v) { return v != ports[i]; });

    std::vector<std::string> out_addrs;
    out_addrs.reserve(out_ports.size());
    std::transform(out_ports.begin(),
                   out_ports.end(),
                   std::back_inserter(out_addrs),
                   [](const auto prt) {
                     return solidarity::utils::strings::to_string("localhost:", prt);
                   });

    solidarity::node::params_t params;
    params.raft_settings.set_max_log_size(1000).set_election_timeout(
        std::chrono::milliseconds(500));
    params.port = start_port + (unsigned short)(i);
    params.client_port = start_c_port + (unsigned short)(i);
    params.thread_count = 1;
    params.cluster = out_addrs;
    params.name = to_string("node_", params.port);

    auto log_prefix = to_string(params.name, "> ");
    auto node_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
        logger_ptr, log_prefix);

    auto state_machine = std::make_shared<dummy_fsm>();
    auto n = std::make_shared<solidarity::node>(node_logger, params, state_machine.get());
    n->start();
    smachines[i] = state_machine;
    nodes[i] = n;

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
    clients[i] = c;
    std::cout << params.name << " was started. " << std::endl;
  }

  std::cout << "election." << std::endl;
  std::unordered_set<solidarity::node_name> leaders;
  while (true) {
    leaders.clear();
    for (auto &kv : nodes) {
      if (kv->state().node_kind == solidarity::NODE_KIND::LEADER) {
        leaders.insert(kv->self_name());
      }
    }
    if (leaders.size() == 1) {
      auto leader_name = *leaders.begin();
      bool election_complete = true;
      for (auto &kv : nodes) {
        auto state = kv->state();
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

  uint64_t v = 0;

  std::vector<double> etimes(writes_count);
  for (size_t i = 0; i < writes_count; ++i) {
    auto cmd = solidarity::command_t::from_value(v);

    auto c = clients[i % node_count];

    solidarity::utils::elapsed_time et;

    if (sync_writes) {
      auto sst = c->send_strong(cmd);
      if (!sst.is_ok()) {
        std::cerr << "command status - crc=" << cmd.crc()
                  << " ecode=" << solidarity::to_string(sst.ecode)
                  << " status=" << solidarity::to_string(sst.status) << std::endl;
      }
    } else {
      auto ec = c->send_weak(cmd);
      if (ec != solidarity::ERROR_CODE::OK) {
        std::cerr << "command status - crc=" << cmd.crc()
                  << " ecode=" << solidarity::to_string(ec) << std::endl;
      }
    }
    etimes[i] = et.elapsed();
    std::cout << "elapsed " << etimes[i] << std::endl;
    ++v;
  }
  auto res = std::accumulate(etimes.cbegin(), etimes.cend(), double(0.0));
  res = res / writes_count;

  std::cout << "middle time: " << res << std::endl;
}