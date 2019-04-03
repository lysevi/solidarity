#include <librft/abstract_cluster.h>
#include <librft/exports.h>
#include <libdialler/message.h>

#include <vector>

namespace rft::queries {
enum class QUERY_KIND : dialler::message::kind_t {
  CONNECT = 0,
  CONNECTION_ERROR,
  COMMAND
};

struct query_connect_t {
  uint16_t protocol_version;
  std::string node_id;
  EXPORT query_connect_t(uint16_t protocol_version_, std::string node_id_) {
    protocol_version = protocol_version_;
    node_id = node_id_;
  }
  EXPORT query_connect_t(const dialler::message_ptr &msg);
  dialler::message_ptr to_message() const;
};

struct connection_error_t {
  uint16_t protocol_version;
  std::string msg;
  EXPORT connection_error_t(uint16_t protocol_version_, const std::string &m) {
    protocol_version = protocol_version_;
    msg = m;
  }
  EXPORT connection_error_t(const dialler::message_ptr &mptr);

  dialler::message_ptr to_message() const;
};

struct command_t {
  append_entries cmd;
  cluster_node from;
  EXPORT command_t(const cluster_node &from_, append_entries cmd_) {
    cmd = cmd_;
    from = from_;
  }
  EXPORT command_t(const std::vector<dialler::message_ptr> &mptr);

  std::vector<dialler::message_ptr> to_message() const;
};

struct client_connect_t {
  uint16_t protocol_version;
  EXPORT client_connect_t(uint16_t protocol_version_) {
    protocol_version = protocol_version_;
  }
  EXPORT client_connect_t(const dialler::message_ptr &msg);
  dialler::message_ptr to_message() const;
};
} // namespace rft::queries