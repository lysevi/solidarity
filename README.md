<p align="left"><img src="artwork/logo.small.png"></p>
<b>
<table>
    <tr>
        <td>
            master branch
        </td>
        <td>
            Linux <a href="https://travis-ci.org/lysevi/solidarity"><img src="https://travis-ci.org/lysevi/solidarity.svg?branch=master"></a>
        </td>
        <td>
            Windows <a href="https://ci.appveyor.com/project/lysevi/solidarity/branch/master"><img src="https://ci.appveyor.com/api/projects/status/xir9ui0vtu9806aq/branch/master?svg=true"></a>
        </td>
        <td>
            <a href="https://coveralls.io/github/lysevi/solidarity?branch=master"><img src="https://coveralls.io/repos/github/lysevi/solidarity/badge.svg?branch=master"></a>
        </td>
        <td>
            <a href="https://codecov.io/gh/lysevi/solidarity"><img src="https://codecov.io/gh/lysevi/solidarity/branch/master/graph/badge.svg"></a>
        </td>
    </tr>
    <tr>
        <td>
            dev branch
        </td>
        <td>
            Linux <a href="https://travis-ci.org/lysevi/solidarity"><img src="https://travis-ci.org/lysevi/solidarity.svg?branch=dev"></a>
        </td>
        <td>
            Windows <a href="https://ci.appveyor.com/project/lysevi/solidarity/branch/dev"><img src="https://ci.appveyor.com/api/projects/status/xir9ui0vtu9806aq/branch/dev?svg=true"></a>
        </td>
        <td>
            <a href="https://coveralls.io/github/lysevi/solidarity?branch=dev"><img src="https://coveralls.io/repos/github/lysevi/solidarity/badge.svg?branch=dev"></a>
        </td>
        <td>
            <a href="https://codecov.io/gh/lysevi/solidarity"><img src="https://codecov.io/gh/lysevi/solidarity/branch/master/graph/badge.svg"></a>
        </td>
    </tr>
</table>
</b>

# SOLIDarity 
C++ implementation of raft consensus.

## Dependencies
---
* Boost 1.69.0 or higher: system, asio, stacktrace, datetime.
* cmake 3.10 or higher
* conan.io 
* c++ 17 compiler (MSVC 2017, gcc 7.0)

## Building
---
```sh
$ git submodule update --init 
$ mkdir build
$ cd build
$ conan install ..
$ cmake ..
$ cmake --build . --config Release 
```
## Example
```C++
/**
running example:
ex_counter.exe -p 10000 --cluster "localhost:10001" "localhost:10002"
*/
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

int main(int , char **) {
  logger_ptr = std::make_shared<solidarity::utils::logging::file_logger>(
      to_string("server_counter_", port), verbose);

  solidarity::node::params_t params;
  params.rft_settings.set_max_log_size(1000).set_election_timeout(
      std::chrono::milliseconds(500));
  params.port = port;
  params.client_port = client_port++;
  params.thread_count = thread_count;
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
```
