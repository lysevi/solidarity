#define CATCH_CONFIG_RUNNER
#include <libutils/logger.h>
#include <catch.hpp>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <list>
#include <sstream>

class UnitTestLogger : public utils::logging::abstract_logger {
public:
  static bool verbose;
  bool _write_to_file;
  std::unique_ptr<std::ofstream> _output;
  UnitTestLogger(bool write_to_file = true) : _write_to_file(write_to_file) {
    if (_write_to_file) {
      auto cur_time = std::chrono::system_clock::now();
      std::time_t tt = std::chrono::system_clock::to_time_t(cur_time);
      tm *ptm = gmtime(&tt);

      std::stringstream fname_ss;
      fname_ss << "solidarity_unittests_";
      char buf[1024];
      std::fill(std::begin(buf), std::end(buf), '\0');
      std::strftime(buf, 1024, "%F", ptm);
      fname_ss << buf;
      fname_ss << ".log";
	  
      auto logname = fname_ss.str();
      if (verbose) {
        std::cout << "See output in " << logname << std::endl;
      }
      _output = std::make_unique<std::ofstream>(logname, std::ofstream::app);
      (*_output) << "Start programm"<<std::endl;
    }
  }
  ~UnitTestLogger() {}

  void message(utils::logging::message_kind kind, const std::string &msg) noexcept {
    std::lock_guard<std::mutex> lg(_locker);

    std::stringstream ss;
    switch (kind) {
    case utils::logging::message_kind::fatal:
      ss << "[err] " << msg << std::endl;
      break;
    case utils::logging::message_kind::info:
      ss << "[inf] " << msg << std::endl;
      break;
    case utils::logging::message_kind::warn:
      ss << "[wrn] " << msg << std::endl;
      break;
    case utils::logging::message_kind::message:
      ss << "[dbg] " << msg << std::endl;
      break;
    }

    if (_write_to_file) {
      (*_output) << ss.str();
	  _output->flush();
      _output->flush();
    }

    if (kind == utils::logging::message_kind::fatal) {
      std::cerr << ss.str();
    } else {
      if (verbose) {
        std::cout << ss.str();
      } else {

        _messages.push_back(ss.str());
      }
    }
  }

  void dump_all() {
    for (auto m : _messages) {
      std::cerr << m;
    }
  }

private:
  std::mutex _locker;
  std::list<std::string> _messages;
};

bool UnitTestLogger::verbose = false;

struct LoggerControl : Catch::TestEventListenerBase {
  using TestEventListenerBase::TestEventListenerBase; // inherit constructor

  virtual void testCaseStarting(Catch::TestCaseInfo const &) override {
    _raw_ptr = new UnitTestLogger();
    _logger = utils::logging::abstract_logger_ptr{_raw_ptr};
    utils::logging::logger_manager::start(_logger);
  }

  virtual void testCaseEnded(Catch::TestCaseStats const &testCaseStats) override {
    if (testCaseStats.testInfo.expectedToFail()) {
      _raw_ptr->dump_all();
    }
    utils::logging::logger_manager::stop();
    _logger = nullptr;
  }
  UnitTestLogger *_raw_ptr;
  utils::logging::abstract_logger_ptr _logger;
};

CATCH_REGISTER_LISTENER(LoggerControl);

int main(int argc, char **argv) {
  int _argc = argc;
  char **_argv = argv;
  for (int i = 0; i < argc; ++i) {
    if (std::strcmp(argv[i], "--verbose") == 0) {
      UnitTestLogger::verbose = true;
      _argc--;
      _argv = new char *[_argc];
      int r_pos = 0, w_pos = 0;
      for (int a = 0; a < argc; ++a) {
        if (a != i) {

          _argv[w_pos] = argv[r_pos];
          w_pos++;
        }
        r_pos++;
      }
      break;
      ;
    }
  }

  Catch::Session sesssion;
  sesssion.configData().showDurations = Catch::ShowDurations::OrNot::Always;
  int result = sesssion.run(_argc, _argv);
  if (UnitTestLogger::verbose) {
    delete[] _argv;
  }
  return (result < 0xff ? result : 0xff);
}
