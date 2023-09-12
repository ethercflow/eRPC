#include "sm_types.h"

namespace erpc {

std::string sm_err_type_str(SmErrType err_type) {
  assert(sm_err_type_is_valid(err_type));

  switch (err_type) {
    case SmErrType::kNoError: return "[No error]";
    case SmErrType::kSrvDisconnected: return "[Server disconnected]";
    case SmErrType::kRingExhausted: return "[Ring buffers exhausted]";
    case SmErrType::kOutOfMemory: return "[Out of memory]";
    case SmErrType::kRoutingResolutionFailure:
      return "[Routing resolution failure]";
    case SmErrType::kInvalidRemoteRpcId: return "[Invalid remote Rpc ID]";
    case SmErrType::kInvalidTransport: return "[Invalid transport]";
  }

  throw std::runtime_error("Invalid session management error type");
}

std::string sm_event_type_str(SmEventType event_type) {
  switch (event_type) {
    case SmEventType::kConnected: return "[Connected]";
    case SmEventType::kConnectFailed: return "[Connect failed]";
    case SmEventType::kDisconnected: return "[Disconnected]";
    case SmEventType::kDisconnectFailed: return "[kDisconnect failed]";
  }
  return "[Invalid event type]";
}

}
