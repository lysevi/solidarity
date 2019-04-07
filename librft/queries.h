#include <librft/abstract_cluster.h>

#include <librft/exports.h>
#include <libdialler/message.h>
#include <vector>

namespace rft::queries {
const uint64_t UNDEFINED_QUERY_ID = std::numeric_limits<uint64_t>::max();

enum class QUERY_KIND : dialler::message::kind_t {
  CONNECT = 0,
  STATUS,
  CONNECTION_ERROR,
  COMMAND,
  READ,
  WRITE,
  UPDATE
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
  EXPORT connection_error_t(uint16_t protocol_version_, const std::string &m) {
    protocol_version = protocol_version_;
    msg = m;
  }
  EXPORT connection_error_t(const dialler::message_ptr &mptr);
  EXPORT dialler::message_ptr to_message() const;
};

struct status_t {
  uint64_t id;
  std::string msg;

  EXPORT status_t(uint64_t id_, const std::string &m) {
    id = id_;
    msg = m;
  }
  EXPORT status_t(const dialler::message_ptr &mptr);
  EXPORT dialler::message_ptr to_message() const;
};

struct command_t {
  append_entries cmd;
  cluster_node from;
  EXPORT command_t(const cluster_node &from_, append_entries cmd_) {
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
  rft::command query;
  EXPORT read_query_t(uint64_t id, const rft::command &q) {
    msg_id = id;
    query = q;
  }
  EXPORT read_query_t(const dialler::message_ptr &msg);
  EXPORT dialler::message_ptr to_message() const;
};

struct write_query_t {
  uint64_t msg_id;
  rft::command query;
  EXPORT write_query_t(uint64_t id, const rft::command &q) {
    msg_id = id;
    query = q;
  }
  EXPORT write_query_t(const dialler::message_ptr &msg);
  EXPORT dialler::message_ptr to_message() const;
};

struct state_machine_updated_t {
  bool f;
  EXPORT state_machine_updated_t();
  EXPORT state_machine_updated_t(const dialler::message_ptr &msg);
  EXPORT dialler::message_ptr to_message() const;
};
} // namespace clients

} // namespace rft::queries