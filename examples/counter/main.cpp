/**
running example:
ex_counter.exe -p 10000 --cluster "localhost:10001" "localhost:10002"
*/
#include <cxxopts.hpp>
#include <iostream>
#include <solidarity/solidarity.h>

class counter_sm final : public solidarity::abstract_state_machine {
public:
  counter_sm()
      : counter(0) {}

  void apply_cmd(const solidarity::command &cmd) override {
    std::lock_guard l(_locker);
    counter = cmd.to_value<uint64_t>();
    std::cout << "add counter: " << counter << std::endl;
  }

  void reset() override {
    std::lock_guard l(_locker);
    counter = 0;
    std::cout << "reset counter: " << counter << std::endl;
  }

  solidarity::command snapshot() override {
    std::shared_lock l(_locker);
    return solidarity::command::from_value<uint64_t>(counter);
  }

  void install_snapshot(const solidarity::command &cmd) override {
    std::lock_guard l(_locker);
    counter = cmd.to_value<uint64_t>();
    std::cout << "install_snapshot counter: " << counter << std::endl;
  }

  solidarity::command read(const solidarity::command & /*cmd*/) override {
    return snapshot();
  }

  bool can_apply(const solidarity::command &) override { return true; }

  std::shared_mutex _locker;
  uint64_t counter;
};

using solidarity::utils::strings::to_string;

solidarity::utils::logging::abstract_logger_ptr logger_ptr = nullptr;

size_t thread_count = 1;
unsigned short port = 10000;
unsigned short client_port = 11000;
std::vector<std::string> cluster;
bool verbose = false;

int main(int argc, char **argv) {
  cxxopts::Options options("Distributed increment", "Example distributed increment");
  options.allow_unrecognised_options();
  options.positional_help("[optional args]").show_positional_help();
  options.parse_positional({"cluster"});

  auto add_o = options.add_options();
  add_o("v,verbose", "Enable debugging");
  add_o("h,help", "Help");
  add_o("t,threads", "Threads for io loop", cxxopts::value<size_t>(thread_count));
  add_o(
      "p,port", "Listening port for other servers", cxxopts::value<unsigned short>(port));
  add_o("c,client",
        "Listening port for clients",
        cxxopts::value<unsigned short>(client_port));
  add_o("cluster", "Cluster nodes addrs", cxxopts::value<std::vector<std::string>>());

  try {
    cxxopts::ParseResult result = options.parse(argc, argv);

    if (result["help"].as<bool>()) {
      std::cout << options.help() << std::endl;

      std::cout << "Usage example:" << std::endl;
      std::cout << " ex_counter.exe -p 10000 --cluster \"localhost:10001\" \"localhost:10002\"" << std::endl;
      std::exit(0);
    }

    if (result["verbose"].as<bool>()) {
      verbose = true;

    } else {
      verbose = false;
    }

    if (result.count("cluster")) {
      auto &v = result["cluster"].as<std::vector<std::string>>();
      cluster = v;
    }

    logger_ptr = std::make_shared<solidarity::utils::logging::file_logger>(
        to_string("server_inc_", port), verbose);
  } catch (cxxopts::OptionException &ex) {
    logger_ptr->fatal(ex.what());
  }

  logger_ptr->info("port: ", port);
  logger_ptr->info("threads: ", thread_count);
  logger_ptr->info("client port: ", client_port);
  std::stringstream ss;
  ss << "cluster: {";
  std::copy(
      cluster.cbegin(), cluster.cend(), std::ostream_iterator<std::string>(ss, " "));
  ss << "}";
  logger_ptr->info(ss.str());

  solidarity::node::params_t params;
  params.rft_settings.set_max_log_size(1000).set_election_timeout(
      std::chrono::milliseconds(500));
  params.port = port;
  params.client_port = client_port++;
  params.thread_count = 1;
  params.cluster = cluster;
  params.name = to_string("node_", port);

  auto log_prefix = to_string(params.name, "> ");
  auto node_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
      logger_ptr, log_prefix);

  auto state_machine = std::make_shared<counter_sm>();
  auto n = std::make_shared<solidarity::node>(node_logger, params, state_machine.get());

  n->start();
  while (true) {
    if (n->state().node_kind == solidarity::NODE_KIND::LEADER) {
      auto v = state_machine->counter + 1;
      auto cmd = solidarity::command::from_value(v);
      auto ec = n->add_command(cmd);
      std::cout << "value:" << v << " ecode:" << to_string(ec) << std::endl;
      ;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}