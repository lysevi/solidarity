#pragma once
#include "helpers.h"
#include <solidarity/client.h>
#include <solidarity/dialler/message.h>
#include <solidarity/node.h>
#include <solidarity/utils/logger.h>
#include <solidarity/utils/strings.h>

#include <catch.hpp>
#include <condition_variable>
#include <iostream>
#include <numeric>

class test_description_t {
public:
  void init(size_t cluster_size, bool add_listener_handler=false);
  virtual std::unordered_map<uint32_t,
                             std::shared_ptr<solidarity::abstract_state_machine>>
  get_state_machines() = 0;

  std::unordered_set<solidarity::node_name> wait_election();

  std::unordered_map<std::string, std::shared_ptr<solidarity::node>> nodes;
  std::unordered_map<
      std::string,
      std::unordered_map<uint32_t, std::shared_ptr<solidarity::abstract_state_machine>>>
      consumers;
  std::unordered_map<std::string, std::shared_ptr<solidarity::client>> clients;
};
