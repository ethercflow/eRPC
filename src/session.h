#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <limits>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>

#include "common.h"
#include "session_mgmt_types.h"
#include "transport_types.h"
#include "util/udp_client.h"

namespace ERpc {

/*
 * Maximum number of sessions (both as client and server) that can be created
 * by a thread through its lifetime. Increase this for more sessions.
 */
static const size_t kMaxSessionsPerThread = 1024;
static_assert(kMaxSessionsPerThread < std::numeric_limits<uint32_t>::max(),
              "Session number must fit in 32 bits");

/*
 * Invalid values that need to be filled in session metadata.
 */
static const uint8_t kInvalidAppTid = std::numeric_limits<uint8_t>::max();
static const uint32_t kInvalidSessionNum = std::numeric_limits<uint32_t>::max();
static const uint64_t kInvalidStartSeq = std::numeric_limits<uint64_t>::max();
static const uint8_t kInvalidPhyPort = std::numeric_limits<uint8_t>::max();

/// Session state that can only go forward.
enum class SessionState {
  kConnectInProgress,
  kConnected,  ///< The only state for server-side sessions
  kDisconnectInProgress,
  kDisconnected,  ///< Temporary state just for the disconnected callback
  kError          /// Only allowed for client-side sessions
};

static std::string session_state_str(SessionState state) {
  switch (state) {
    case SessionState::kConnectInProgress:
      return std::string("[Connect in progress]");
    case SessionState::kConnected:
      return std::string("[Connected]");
    case SessionState::kDisconnectInProgress:
      return std::string("[Disconnect in progress]");
    case SessionState::kDisconnected:
      return std::string("[Disconnected]");
    case SessionState::kError:
      return std::string("[Error]");
  }
  return std::string("[Invalid state]");
}

/// Events generated for application-level session management handler
enum class SessionMgmtEventType {
  kConnected,
  kConnectFailed,
  kDisconnected,
  kDisconnectFailed
};

static std::string session_mgmt_event_type_str(
    SessionMgmtEventType event_type) {
  switch (event_type) {
    case SessionMgmtEventType::kConnected:
      return std::string("[Connected]");
    case SessionMgmtEventType::kConnectFailed:
      return std::string("[Connect failed]");
    case SessionMgmtEventType::kDisconnected:
      return std::string("[Disconnected]");
    case SessionMgmtEventType::kDisconnectFailed:
      return std::string("[kDisconnect failed]");
  }
  return std::string("[Invalid event type]");
}

/// Basic info about a session emd point filled in during initialization.
class SessionMetadata {
 public:
  TransportType transport_type;
  char hostname[kMaxHostnameLen];
  uint8_t app_tid;   ///< TID of the Rpc that created this endpoint
  uint8_t phy_port;  ///< Fabric port used by this endpoint
  uint32_t session_num;
  uint64_t start_seq;
  RoutingInfo routing_info;

  /* Fill invalid metadata to aid debugging */
  SessionMetadata() {
    transport_type = TransportType::kInvalidTransport;
    memset((void *)hostname, 0, sizeof(hostname));
    app_tid = kInvalidAppTid;
    phy_port = kInvalidPhyPort;
    session_num = kInvalidSessionNum;
    start_seq = kInvalidStartSeq;
    memset((void *)&routing_info, 0, sizeof(routing_info));
  }

  /// Return a string with a name for this session endpoint, containing
  /// its hostname, Rpc TID, and the session number.
  inline std::string name() {
    std::ostringstream ret;
    std::string session_num_str = (session_num == kInvalidSessionNum)
                                      ? "XX"
                                      : std::to_string(session_num);

    ret << "[H: " << trim_hostname(hostname)
        << ", R: " << std::to_string(app_tid) << ", S: " << session_num_str
        << "]";
    return ret.str();
  }

  /// Return a string with the name of the Rpc hosting this session endpoint.
  inline std::string rpc_name() {
    std::ostringstream ret;
    ret << "[H: " << trim_hostname(hostname)
        << ", R: " << std::to_string(app_tid) << "]";
    return ret.str();
  }

  /// Compare the location fields of two SessionMetadata objects. This does not
  /// account for non-location fields (e.g., fabric port, routing info).
  bool operator==(const SessionMetadata &other) {
    return strcmp(hostname, other.hostname) == 0 && app_tid == other.app_tid &&
           session_num == other.session_num;
  }
};

/// General-purpose session management packet sent by both Rpc clients and
/// servers. This is pretty large (~500 bytes), so use sparingly.
class SessionMgmtPkt {
 public:
  SessionMgmtPktType pkt_type;
  SessionMgmtErrType err_type; /* For responses only */

  /*
   * Each session management packet contains two copies of session metadata,
   * filled in by the client and server Rpc.
   */
  SessionMetadata client, server;

  SessionMgmtPkt() {}
  SessionMgmtPkt(SessionMgmtPktType pkt_type) : pkt_type(pkt_type) {}

  /// Send this session management packet "as is"
  inline void send_to(const char *dst_hostname,
                      const udp_config_t *udp_config) {
    assert(dst_hostname != NULL);

    UDPClient udp_client(dst_hostname, udp_config->mgmt_udp_port,
                         udp_config->drop_prob);
    ssize_t ret = udp_client.send((char *)this, sizeof(*this));
    _unused(ret);
    assert(ret == (ssize_t)sizeof(*this));
  }

  /**
   * @brief Send the response to this session management request packet, using
   * this packet as the response buffer. This function mutates the packet: it
   * flips the packet type to response, and fills in the response type.
   */
  inline void send_resp_mut(SessionMgmtErrType _err_type,
                            const udp_config_t *udp_config) {
    assert(session_mgmt_is_pkt_type_req(pkt_type));
    pkt_type = session_mgmt_pkt_type_req_to_resp(pkt_type);
    err_type = _err_type;

    send_to(client.hostname, udp_config);
  }
};
static_assert(sizeof(SessionMgmtPkt) < 1400,
              "Session management packet too large for UDP");

/// A one-to-one session class for all transports
class Session {
 public:
  enum class Role : bool { kServer, kClient };

  Session(Role role, SessionState state);
  ~Session();

  std::string get_client_name();

  /// Enable congestion control for this session
  void enable_congestion_control();

  /// Disable congestion control for this session
  void disable_congestion_control();

  Role role;           ///< The role (server/client) of this session endpoint
  SessionState state;  ///< The management state of this session endpoint
  SessionMetadata client, server;  ///< The two endpoints of this session
  uint64_t mgmt_req_tsc;           ///< Timestamp of the last management request
  bool is_cc;  ///< True if congestion control is enabled for this session
};

typedef void (*session_mgmt_handler_t)(Session *, SessionMgmtEventType,
                                       SessionMgmtErrType, void *);

/**
 * @brief An object created by the per-thread Rpc, and shared with the
 * per-process Nexus. All accesses must be done with @session_mgmt_mutex locked.
 */
class SessionMgmtHook {
 public:
  uint8_t app_tid; /* App-level thread ID of the RPC that created this hook */
  std::mutex session_mgmt_mutex;
  volatile size_t session_mgmt_ev_counter; /* Number of session mgmt events */
  std::vector<SessionMgmtPkt *> session_mgmt_pkt_list;

  SessionMgmtHook() : session_mgmt_ev_counter(0) {}
};

}  // End ERpc

#endif  // ERPC_SESSION_H
