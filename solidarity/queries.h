#include <solidarity/abstract_cluster.h>
#include <solidarity/error_codes.h>
#include <solidarity/exports.h>
#include <solidarity/node_kind.h>
#include <solidarity/dialler/message.h>
#include <vector>

namespace solidarity::queries {
const uint64_t UNDEFINED_QUERY_ID = std::numeric_limits<uint64_t>::max();

enum class QUERY_KIND : dialler::message::kind_t {
  CONNECT = 0,
  STATUS,
  CONNECTION_ERROR,
  COMMAND,
  READ,
  WRITE,
  UPDATE,
  RAFT_STATE_UPDATE
};

struct query_connect_t {
  uint16_t protocol_version;
  std::string node_id;
  EXPORT query_connect_t(uint16_t protocol_version_, std::string node_id_) {
    protocol_version = protocol_version_;
    node_id = node_id_;
  }
  EXPORT query_connect_t(const dialler::message_ptr &msg);
  EXPORT dialler::message_ptr to_message() const;
};

struct connection_error_t {
  uint16_t protocol_version;
  std::string msg;
  ERROR_CODE status;

  EXPORT connection_error_t(uint16_t protocol_version_,
                            ERROR_CODE status_,
                            const std::string &m) {
    protocol_version = protocol_version_;
    msg = m;
    status = status_;
  }
  EXPORT connection_error_t(const dialler::message_ptr &mptr);
  EXPORT dialler::message_ptr to_message() const;
};

struct status_t {
  uint64_t id;
  std::string msg;
  ERROR_CODE status;

  EXPORT status_t(uint64_t id_, ERROR_CODE status_, const std::string &m) {
    id = id_;
    msg = m;
    status = status_;
  }

  EXPORT status_t(const dialler::message_ptr &mptr);
  EXPORT dialler::message_ptr to_message() const;
};

struct command_t {
  append_entries cmd;
  node_name from;
  EXPORT command_t(const node_name &from_, append_entries cmd_) {
    cmd = cmd_;
    from = from_;
  }
  EXPORT command_t(const std::vector<dialler::message_ptr> &mptr);
  EXPORT std::vector<dialler::message_ptr> to_message() const;
};

namespace clients {
struct client_connect_t {
  uint16_t protocol_version;
  std::string client_name;
  EXPORT client_connect_t(const std::string &client_name_, uint16_t protocol_version_) {
    protocol_version = protocol_version_;
    client_name = client_name_;
  }
  EXPORT client_connect_t(const dialler::message_ptr &msg);
  EXPORT dialler::message_ptr to_message() const;
};

struct read_query_t {
  uint64_t msg_id;
  solidarity::command query;
  EXPORT read_query_t(uint64_t id, const solidarity::command &q) {
    msg_id = id;
    query = q;
  }
  EXPORT read_query_t(const std::vector<dialler::message_ptr> &msg);
  EXPORT std::vector<dialler::message_ptr> to_message() const;
};

struct write_query_t {
  uint64_t msg_id;
  solidarity::command query;
  EXPORT write_query_t(uint64_t id, const solidarity::command &q) {
    msg_id = id;
    query = q;
  }
  EXPORT write_query_t(const std::vector<dialler::message_ptr> &msg);
  EXPORT std::vector<dialler::message_ptr> to_message() const;
};

struct state_machine_updated_t {
  bool f;
  EXPORT state_machine_updated_t();
  EXPORT state_machine_updated_t(const dialler::message_ptr &msg);
  EXPORT dialler::message_ptr to_message() const;
};

struct raft_state_updated_t {
  NODE_KIND old_state;
  NODE_KIND new_state;
  EXPORT raft_state_updated_t(NODE_KIND from, NODE_KIND to);
  EXPORT raft_state_updated_t(const dialler::message_ptr &msg);
  EXPORT dialler::message_ptr to_message() const;
};
} // namespace clients

} // namespace solidarity::queries