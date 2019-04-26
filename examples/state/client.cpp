#include "common.h"
#include <cxxopts.hpp>
#include <iostream>
#include <solidarity/client.h>
#include <solidarity/utils/strings.h>
#include <solidarity/utils/utils.h>

size_t thread_count = 1;
unsigned short port = 11000;
std::string host = "localhost";
bool strong = false;

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
  add_o("strong", "Use send_strong for to send messages to cluster.");

  try {
    cxxopts::ParseResult result = options.parse(argc, argv);

    if (result["help"].as<bool>()) {
      std::cout << options.help() << std::endl;
      std::exit(0);
    }

    if (result["strong"].as<bool>()) {
      strong = true;
    }

  } catch (cxxopts::OptionException &ex) {
    std::cerr << ex.what() << std::endl;
  }

  std::cout << "port: " << port << std::endl;
  std::cout << "threads: " << thread_count << std::endl;
  std::cout << "host: " << host << std::endl;
  std::cout << "strong: " << strong << std::endl;

  solidarity::client::params_t cpar("client_");
  cpar.threads_count = 1;
  cpar.host = host;
  cpar.port = port;

  uint64_t i = 0;

  auto c = std::make_shared<solidarity::client>(cpar);
  c->connect();
  if (c->is_connected()) {
    auto answer = c->read({});
    if (!answer.is_empty()) {
      i = answer.to_value<uint64_t>();
      std::cout << "read i:" << i << std::endl;
    }
  }

  while (!c->is_connected()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  bool is_network_error = false;
  while (true) {
    try {
      std::cout << "write i:" << i << std::endl;
      solidarity::utils::elapsed_time el;
      if (strong) {
        auto sst = c->send_strong(solidarity::command::from_value(i));
        if (!sst.is_ok()) {
          std::cerr << "command status - cmd=" << i
                    << " ecode=" << solidarity::to_string(sst.ecode)
                    << " status=" << solidarity::to_string(sst.status) << std::endl;
        }
        is_network_error = sst.ecode == solidarity::ERROR_CODE::NETWORK_ERROR;
      } else {
        auto ec = c->send_weak(solidarity::command::from_value(i));
        if (ec != solidarity::ERROR_CODE::OK) {
          is_network_error = ec == solidarity::ERROR_CODE::NETWORK_ERROR;
          continue;
        }
        auto answer = c->read({});
        std::cout << "sz: " << answer.size() << std::endl;
      }

      std::cout << "elapsed: " << el.elapsed() << std::endl;

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      ++i;
      if (is_network_error) {
        break;
      }
    } catch (solidarity::exception &e) {
      std::cerr << e.what() << std::endl;
      return 0;
    }
  }
}