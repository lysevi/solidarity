#include <solidarity/abstract_cluster.h>
#include <solidarity/queries.h>

#include "helpers.h"
#include <catch.hpp>
#include <numeric>

void check_append_entries(const solidarity::append_entries &ae,
                          const solidarity::append_entries &res) {
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
  solidarity::append_entries ae;

  solidarity::ENTRIES_KIND kind = solidarity::ENTRIES_KIND::HEARTBEAT;
  solidarity::node_name leader;
  solidarity::logdb::LOG_ENTRY_KIND lk = solidarity::logdb::LOG_ENTRY_KIND::APPEND;

  SECTION("kind=HEARTBEAT") { kind = solidarity::ENTRIES_KIND::HEARTBEAT; }
  SECTION("kind=VOTE") { kind = solidarity::ENTRIES_KIND::VOTE; }
  SECTION("kind=APPEND") { kind = solidarity::ENTRIES_KIND::APPEND; }
  SECTION("kind=ANSWER_OK") { kind = solidarity::ENTRIES_KIND::ANSWER_OK; }
  SECTION("kind=ANSWER_FAILED") { kind = solidarity::ENTRIES_KIND::ANSWER_FAILED; }
  SECTION("kind=HELLO") { kind = solidarity::ENTRIES_KIND::HELLO; }

  SECTION("leader=LEADER") { leader.set_name("LEADER"); }
  SECTION("leader.is_empty()") { leader = solidarity::node_name(); }

  SECTION("cmd.is_empty()") { ae.cmd.data.clear(); }
  SECTION("!cmd.is_empty() [small]") {
    ae.cmd.data = std::vector<uint8_t>({1, 2, 3, 4, 5});
  }
  SECTION("!cmd.is_empty() [big]") {
    ae.cmd.resize(1000);
    std::iota(ae.cmd.data.begin(), ae.cmd.data.end(), uint8_t(0));
  }

  SECTION("log_entry_kind:SNAPSHOT") { lk = solidarity::logdb::LOG_ENTRY_KIND::SNAPSHOT; }
  SECTION("log_entry_kind:APPEND") { lk = solidarity::logdb::LOG_ENTRY_KIND::APPEND; }

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
  auto res = solidarity::append_entries::from_byte_array(packed);
  check_append_entries(ae, res);
}

TEST_CASE("serialisation.query_connect", "[network]") {
  solidarity::queries::query_connect_t qc(777, "node id");
  auto msg = qc.to_message();
  solidarity::queries::query_connect_t qc_u(msg);
  EXPECT_EQ(
      msg->get_header()->kind,
      (solidarity::dialler::message::kind_t)solidarity::queries::QUERY_KIND::CONNECT);
  EXPECT_EQ(qc.protocol_version, qc_u.protocol_version);
  EXPECT_EQ(qc.node_id, qc_u.node_id);
}

TEST_CASE("serialisation.connection_error", "[network]") {
  solidarity::queries::connection_error_t qc(
      777, solidarity::ERROR_CODE::WRONG_PROTOCOL_VERSION, "");

  SECTION("empty message") { qc.msg = std::string(); }
  SECTION("long message") {
    qc.msg = "long error message! long error message! long error message";
  }

  auto msg = qc.to_message();
  solidarity::queries::connection_error_t qc_u(msg);
  EXPECT_EQ(msg->get_header()->kind,
            (solidarity::dialler::message::kind_t)
                solidarity::queries::QUERY_KIND::CONNECTION_ERROR);
  EXPECT_EQ(qc.protocol_version, qc_u.protocol_version);
  EXPECT_EQ(qc.msg, qc_u.msg);
  EXPECT_EQ(qc.status, qc_u.status);
}

TEST_CASE("serialisation.status_t", "[network]") {
  solidarity::ERROR_CODE s = solidarity::ERROR_CODE::OK;
  SECTION("serialisation.status_t::OK") { s = solidarity::ERROR_CODE::OK; }
  SECTION("serialisation.status_t::NOT_A_LEADER") {
    s = solidarity::ERROR_CODE::NOT_A_LEADER;
  }
  SECTION("serialisation.status_t::CONNECTION_NOT_FOUND") {
    s = solidarity::ERROR_CODE::CONNECTION_NOT_FOUND;
  }
  SECTION("serialisation.status_t::WRONG_PROTOCOL_VERSION") {
    s = solidarity::ERROR_CODE::WRONG_PROTOCOL_VERSION;
  }
  SECTION("serialisation.status_t::UNDER_ELECTION") {
    s = solidarity::ERROR_CODE::UNDER_ELECTION;
  }
  SECTION("serialisation.status_t::UNDEFINED") { s = solidarity::ERROR_CODE::UNDEFINED; }

  solidarity::queries::status_t qc(777, s, "");

  SECTION("empty message") { qc.msg = std::string(); }
  SECTION("long message") {
    qc.msg = "long error message! long error message! long error message";
  }

  auto msg = qc.to_message();
  solidarity::queries::status_t qc_u(msg);
  EXPECT_EQ(
      msg->get_header()->kind,
      (solidarity::dialler::message::kind_t)solidarity::queries::QUERY_KIND::STATUS);
  EXPECT_EQ(qc.id, qc_u.id);
  EXPECT_EQ(qc.msg, qc_u.msg);
  EXPECT_EQ(qc.status, s);
}

TEST_CASE("serialisation.command", "[network]") {
  solidarity::append_entries ae;

  solidarity::ENTRIES_KIND kind = solidarity::ENTRIES_KIND::HEARTBEAT;
  solidarity::node_name leader;
  solidarity::logdb::LOG_ENTRY_KIND lk = solidarity::logdb::LOG_ENTRY_KIND::APPEND;

  kind = solidarity::ENTRIES_KIND::HEARTBEAT;
  leader.set_name("LEADER");
  SECTION("small cmd") {
    ae.cmd.resize(100);
    std::iota(ae.cmd.data.begin(), ae.cmd.data.end(), uint8_t(0));
  }
  SECTION("big cmd") {
    ae.cmd.resize(solidarity::dialler::message::MAX_BUFFER_SIZE * 11);
    std::iota(ae.cmd.data.begin(), ae.cmd.data.end(), uint8_t(0));
  }
  lk = solidarity::logdb::LOG_ENTRY_KIND::APPEND;

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

  solidarity::queries::command_t cmd(solidarity::node_name("from node name"), ae);

  auto msg = cmd.to_message();
  solidarity::queries::command_t cmd_u(msg);
  EXPECT_EQ(cmd.from, cmd_u.from);
  EXPECT_EQ(
      msg.front()->get_header()->kind,
      (solidarity::dialler::message::kind_t)solidarity::queries::QUERY_KIND::COMMAND);
  check_append_entries(ae, cmd_u.cmd);
}

TEST_CASE("serialisation.client_connect_t", "[network]") {
  solidarity::queries::clients::client_connect_t qc("client name", 777);
  auto msg = qc.to_message();
  solidarity::queries::clients::client_connect_t qc_u(msg);
  EXPECT_EQ(
      msg->get_header()->kind,
      (solidarity::dialler::message::kind_t)solidarity::queries::QUERY_KIND::CONNECT);
  EXPECT_EQ(qc.protocol_version, qc_u.protocol_version);
  EXPECT_EQ(qc.client_name, qc_u.client_name);
}

TEST_CASE("serialisation.read_query_t", "[network]") {
  solidarity::command cmd;
  cmd.data = std::vector<uint8_t>{0, 1, 2, 3};

  SECTION("small cmd") {
    cmd.data.resize(100);
    std::iota(cmd.data.begin(), cmd.data.end(), uint8_t(0));
  }
  SECTION("big cmd") {
    cmd.data.resize(solidarity::dialler::message::MAX_BUFFER_SIZE * 11);
    std::iota(cmd.data.begin(), cmd.data.end(), uint8_t(0));
  }

  solidarity::queries::clients::read_query_t qc(777, cmd);
  auto msg = qc.to_message();

  for (auto &v : msg) {
    EXPECT_EQ(
        v->get_header()->kind,
        (solidarity::dialler::message::kind_t)solidarity::queries::QUERY_KIND::READ);
  }

  solidarity::queries::clients::read_query_t qc_u(msg);

  EXPECT_EQ(qc.msg_id, qc_u.msg_id);
  EXPECT_TRUE(std::equal(qc.query.data.begin(),
                         qc.query.data.end(),
                         qc_u.query.data.begin(),
                         qc_u.query.data.end()));
  EXPECT_TRUE(std::equal(
      qc.query.data.begin(), qc.query.data.end(), cmd.data.begin(), cmd.data.end()));
}

TEST_CASE("serialisation.write_query_t", "[network]") {
  solidarity::command cmd;
  cmd.data = std::vector<uint8_t>{0, 1, 2, 3};

  SECTION("small cmd") {
    cmd.data.resize(100);
    std::iota(cmd.data.begin(), cmd.data.end(), uint8_t(0));
  }
  SECTION("big cmd") {
    cmd.data.resize(solidarity::dialler::message::MAX_BUFFER_SIZE * 11);
    std::iota(cmd.data.begin(), cmd.data.end(), uint8_t(0));
  }

  solidarity::queries::clients::write_query_t qc(777, cmd);
  auto msg = qc.to_message();

  for (auto &v : msg) {
    EXPECT_EQ(
        v->get_header()->kind,
        (solidarity::dialler::message::kind_t)solidarity::queries::QUERY_KIND::WRITE);
  }

  solidarity::queries::clients::write_query_t qc_u(msg);

  EXPECT_EQ(qc.msg_id, qc_u.msg_id);
  EXPECT_TRUE(std::equal(qc.query.data.begin(),
                         qc.query.data.end(),
                         qc_u.query.data.begin(),
                         qc_u.query.data.end()));
  EXPECT_TRUE(std::equal(
      qc.query.data.begin(), qc.query.data.end(), cmd.data.begin(), cmd.data.end()));
}

TEST_CASE("serialisation.state_machine_updated_t", "[network]") {
  solidarity::state_machine_updated_event_t smev;
  smev.crc = 33;
  smev.kind = solidarity::state_machine_updated_event_t::event_kind::WAS_APPLIED;
  solidarity::queries::clients::state_machine_updated_t qc(smev);
  auto msg = qc.to_message();

  solidarity::queries::clients::state_machine_updated_t qc_u(msg);
  EXPECT_EQ(
      msg->get_header()->kind,
      (solidarity::dialler::message::kind_t)solidarity::queries::QUERY_KIND::UPDATE);
  EXPECT_EQ(qc_u.e.crc, qc.e.crc);
  EXPECT_EQ(qc_u.e.kind, qc.e.kind);
}

TEST_CASE("serialisation.raft_state_updated_t", "[network]") {
  solidarity::NODE_KIND k = solidarity::NODE_KIND::CANDIDATE;
  SECTION("CANDIDATE") { k = solidarity::NODE_KIND::CANDIDATE; }
  SECTION("FOLLOWER") { k = solidarity::NODE_KIND::FOLLOWER; }
  SECTION("ELECTION") { k = solidarity::NODE_KIND::ELECTION; }
  SECTION("LEADER") { k = solidarity::NODE_KIND::LEADER; }

  solidarity::queries::clients::raft_state_updated_t qc(solidarity::NODE_KIND::FOLLOWER,
                                                        k);
  auto msg = qc.to_message();

  solidarity::queries::clients::raft_state_updated_t qc_u(msg);
  EXPECT_EQ(msg->get_header()->kind,
            (solidarity::dialler::message::kind_t)
                solidarity::queries::QUERY_KIND::RAFT_STATE_UPDATE);
  EXPECT_EQ(qc_u.new_state, k);
  EXPECT_EQ(qc_u.old_state, solidarity::NODE_KIND::FOLLOWER);
}