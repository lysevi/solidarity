#include "helpers.h"
#include <librft/consensus.h>
#include <catch.hpp>
#include <map>

struct mock_cluster : rft::abstract_cluster {
  void send_to(rft::cluster_node &from, rft::cluster_node &to,
               const rft::append_entries &m) override {}
  size_t size() override { return sz; }

  size_t sz = 1;
  std::map<rft::cluster_node, std::shared_ptr<rft::consensus>> _cluster;
};

TEST_CASE("consensus") {
  auto settings = rft::node_settings().set_name("_0").set_election_timeout(
      std::chrono::milliseconds(500));

  auto cluster = std::make_shared<mock_cluster>();
  auto c_0 = std::make_shared<rft::consensus>(settings, cluster,
                                              rft::logdb::memory_journal::make_new());
  cluster->_cluster[rft::cluster_node().set_name("_0")] = c_0;
  EXPECT_EQ(c_0->round(), rft::round_t(0));
  EXPECT_EQ(c_0->state(), rft::CONSENSUS_STATE::FOLLOWER);

  SECTION("consensus.single") {
    while (c_0->state() != rft::CONSENSUS_STATE::LEADER) {
      c_0->on_timer();
      rft::utils::sleep_mls(100);
    }
    EXPECT_EQ(c_0->round(), rft::round_t(1));
  }
}