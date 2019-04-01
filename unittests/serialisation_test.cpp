#include <librft/abstract_cluster.h>
#include <librft/queries.h>

#include "helpers.h"
#include <catch.hpp>

void check_append_entries(const rft::append_entries &ae, const rft::append_entries &res) {
  EXPECT_EQ(ae.term, res.term);
  EXPECT_EQ(ae.kind, res.kind);
  EXPECT_EQ(ae.starttime, res.starttime);
  EXPECT_EQ(ae.leader.name(), res.leader.name());

  EXPECT_EQ(ae.current, res.current);
  EXPECT_EQ(ae.current.kind, res.current.kind);
  EXPECT_EQ(ae.prev, res.prev);
  EXPECT_EQ(ae.prev.kind, res.prev.kind);
  EXPECT_EQ(ae.commited, res.commited);
  EXPECT_EQ(ae.commited.kind, res.commited.kind);

  auto is_eq = std::equal(ae.cmd.data.begin(), ae.cmd.data.end(), res.cmd.data.begin());
  EXPECT_TRUE(is_eq);
}

TEST_CASE("serialisation.append_entries", "[network]") {
  rft::append_entries ae;

  rft::ENTRIES_KIND kind = rft::ENTRIES_KIND::HEARTBEAT;
  rft::cluster_node leader;
  rft::logdb::LOG_ENTRY_KIND lk = rft::logdb::LOG_ENTRY_KIND::APPEND;

  SECTION("kind=HEARTBEAT") { kind = rft::ENTRIES_KIND::HEARTBEAT; }
  SECTION("kind=VOTE") { kind = rft::ENTRIES_KIND::VOTE; }
  SECTION("kind=APPEND") { kind = rft::ENTRIES_KIND::APPEND; }
  SECTION("kind=ANSWER_OK") { kind = rft::ENTRIES_KIND::ANSWER_OK; }
  SECTION("kind=ANSWER_FAILED") { kind = rft::ENTRIES_KIND::ANSWER_FAILED; }
  SECTION("kind=HELLO") { kind = rft::ENTRIES_KIND::HELLO; }

  SECTION("leader=LEADER") { leader.set_name("LEADER"); }
  SECTION("leader.is_empty()") { leader = rft::cluster_node(); }

  SECTION("cmd.is_empty()") { ae.cmd.data.clear(); }
  SECTION("!cmd.is_empty() [small]") {
    ae.cmd.data = std::vector<uint8_t>({1, 2, 3, 4, 5});
  }
  SECTION("!cmd.is_empty() [big]") {
    ae.cmd.data.resize(1000);
    std::iota(ae.cmd.data.begin(), ae.cmd.data.end(), uint8_t(0));
  }

  SECTION("log_entry_kind:SNAPSHOT") { lk = rft::logdb::LOG_ENTRY_KIND::SNAPSHOT; }
  SECTION("log_entry_kind:APPEND") { lk = rft::logdb::LOG_ENTRY_KIND::APPEND; }

  ae.term = 777;
  ae.kind = kind;
  ae.starttime = 351;
  ae.leader = leader;

  ae.current.kind = lk;
  ae.current.lsn = 77;
  ae.current.term = 88;

  ae.prev.kind = lk;
  ae.prev.lsn = 66;
  ae.prev.term = 99;

  ae.commited.kind = lk;
  ae.commited.lsn = 11;
  ae.commited.term = 22;

  auto packed = ae.to_byte_array();
  auto res = rft::append_entries::from_byte_array(packed);
  check_append_entries(ae, res);
}

TEST_CASE("serialisation.query_connect", "[network]") {
  rft::queries::query_connect_t qc(777, "node id");
  auto msg = qc.to_message();
  rft::queries::query_connect_t qc_u(msg);
  EXPECT_EQ(msg->get_header()->kind,
            (dialler::message::kind_t)rft::queries::QUERY_KIND::CONNECT);
  EXPECT_EQ(qc.protocol_version, qc_u.protocol_version);
  EXPECT_EQ(qc.node_id, qc_u.node_id);
}

TEST_CASE("serialisation.connection_error", "[network]") {
  rft::queries::connection_error_t qc(777, "");

  SECTION("empty message") { qc.msg = std::string(); }
  SECTION("long message") {
    qc.msg = "long error message! long error message! long error message";
  }

  auto msg = qc.to_message();
  rft::queries::connection_error_t qc_u(msg);
  EXPECT_EQ(msg->get_header()->kind,
            (dialler::message::kind_t)rft::queries::QUERY_KIND::CONNECTION_ERROR);
  EXPECT_EQ(qc.protocol_version, qc_u.protocol_version);
  EXPECT_EQ(qc.msg, qc_u.msg);
}

TEST_CASE("serialisation.command", "[network]") {
  rft::append_entries ae;

  rft::ENTRIES_KIND kind = rft::ENTRIES_KIND::HEARTBEAT;
  rft::cluster_node leader;
  rft::logdb::LOG_ENTRY_KIND lk = rft::logdb::LOG_ENTRY_KIND::APPEND;

  kind = rft::ENTRIES_KIND::HEARTBEAT;
  leader.set_name("LEADER");
  ae.cmd.data.resize(1000);
  std::iota(ae.cmd.data.begin(), ae.cmd.data.end(), uint8_t(0));
  lk = rft::logdb::LOG_ENTRY_KIND::APPEND;

  ae.term = 777;
  ae.kind = kind;
  ae.starttime = 351;
  ae.leader = leader;

  ae.current.kind = lk;
  ae.current.lsn = 77;
  ae.current.term = 88;

  ae.prev.kind = lk;
  ae.prev.lsn = 66;
  ae.prev.term = 99;

  ae.commited.kind = lk;
  ae.commited.lsn = 11;
  ae.commited.term = 22;

  rft::queries::command_t cmd(rft::cluster_node("from node name"), ae);

  auto msg = cmd.to_message();
  rft::queries::command_t cmd_u(msg);
  EXPECT_EQ(cmd.from, cmd_u.from);
  EXPECT_EQ(msg->get_header()->kind,
            (dialler::message::kind_t)rft::queries::QUERY_KIND::COMMAND);
  check_append_entries(ae, cmd_u.cmd);
}