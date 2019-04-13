#include <cxxopts.hpp>
#include <iostream>
#include <libsolidarity/client.h>

#include "common.h"

size_t thread_count = 1;
unsigned short port = 11000;
std::string host = "localhost";

int main(int argc, char **argv) {
  cxxopts::Options options("Distributed increment", "Example distributed increment");
  options.allow_unrecognised_options();
  options.positional_help("[optional args]").show_positional_help();

  auto add_o = options.add_options();
  add_o("h,help", "Help");
  add_o("t,threads", "Threads for io loop", cxxopts::value<size_t>(thread_count));
  add_o("host", "Node addr", cxxopts::value<std::string>(host));
  add_o(
      "p,port", "Listening port for other servers", cxxopts::value<unsigned short>(port));

  try {
    cxxopts::ParseResult result = options.parse(argc, argv);

    if (result["help"].as<bool>()) {
      std::cout << options.help() << std::endl;
      std::exit(0);
    }

  } catch (cxxopts::OptionException &ex) {
    std::cerr << ex.what() << std::endl;
  }

  std::cout << "port: " << port << std::endl;
  std::cout << "threads: " << thread_count << std::endl;
  std::cout << "host: " << host << std::endl;

  solidarity::client::params_t cpar("client_");
  cpar.threads_count = 1;
  cpar.host = host;
  cpar.port = port;

  auto c = std::make_shared<solidarity::client>(cpar);
  c->connect();

  while (!c->is_connected()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}