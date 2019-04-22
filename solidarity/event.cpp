#include <solidarity/event.h>
#include <solidarity/utils/utils.h>

std::string solidarity::to_string(const client_event_t &cev) {
  std::stringstream ss;
  ss << "{kind:";
  switch (cev.kind) {
  case client_event_t::event_kind::NETWORK:
    ss << "NETWORK, ecode: " << to_string(cev.net_ev.value().ecode);
    break;
  case client_event_t::event_kind::RAFT:
    auto re = cev.raft_ev.value();
    ss << "RAFT, from:" << to_string(re.old_state)
       << " => to:" << to_string(re.new_state);
    break;
  case client_event_t::event_kind::STATE_MACHINE:
    ss << "STATE_MACHINE";
    break;
  default:
    NOT_IMPLEMENTED;
  }
  ss << "}";
  return ss.str();
}