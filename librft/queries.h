#include <librft/exports.h>
#include <libdialler/message.h>

namespace rft::queries {
enum class QUERY_KIND : dialler::message::kind_t { CONNECT = 0, CONNECTION_ERROR };

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
} // namespace rft::queries