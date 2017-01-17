#ifndef ERPC_RPC_H
#define ERPC_RPC_H

#include <random>
#include <vector>
#include "common.h"
#include "nexus.h"
#include "session.h"
#include "transport.h"
#include "util/buffer.h"
#include "util/rand.h"

namespace ERpc {

static const size_t kSessionMgmtRetransMs = 5;  /* Retransmission for sm reqs */
static const size_t kSessionMgmtTimeoutMs = 50; /* Timeout for sm reqs */

#define rpc_dprintf(fmt, ...)            \
  do {                                   \
    if (RPC_DPRINTF) {                   \
      fprintf(stderr, fmt, __VA_ARGS__); \
      fflush(stderr);                    \
    }                                    \
  } while (0)

// Per-thread RPC object
template <class Transport_>
class Rpc {
  const uint64_t kStartSeqMask = ((1ull << 48) - 1ull);

  struct in_flight_req_t {
    uint64_t prev_tsc; /* The tsc at which a request was last sent */
    Session *session;

    in_flight_req_t(uint64_t tsc, Session *session)
        : prev_tsc(tsc), session(session) {}

    /*
     * The in-flight request vector contains distinct sessions, so we can
     * ignore the timestamp for comparison.
     */
    bool operator==(const in_flight_req_t &other) {
      return (session == other.session);
    }
  };

 public:
  // rpc.cc
  Rpc(Nexus *nexus, void *context, size_t app_tid,
      session_mgmt_handler_t session_mgmt_handler,
      std::vector<size_t> fdev_port_vec);

  ~Rpc();

  // rpc_sm_api.cc

  /**
   * @brief Create a Session and initiate session connection.
   *
   * @return A pointer to the created session if creation succeeds; a callback
   * will be invoked later when connection establishment succeeds/fails.
   * NULL if creation fails; a callback will not be invoked.
   */
  Session *create_session(size_t local_fdev_port_index,
                          const char *_rem_hostname, size_t rem_app_tid,
                          size_t rem_fdev_port_index);

  /**
   * @brief Disconnect and destroy a client session. \p session should not
   * be used by the application after this function is called.
   *
   * @param session A session that was returned by create_session().
   *
   * @return True if (a) the session disconnect packet was sent, and the
   * disconnect callback will be invoked later, or if (b) there was no need for
   * a disconnect packet since the session is in an error state. In the latter
   * case, the callback is invoked before this function returns.
   *
   * False if the session cannot be disconnected right now since connection
   * establishment is in progress , or if the \p session argument is invalid.
   */
  bool destroy_session(Session *session);

  // rpc_datapath.cc
  void send_request(const Session *session, const Buffer *buffer);
  void send_response(const Session *session, const Buffer *buffer);

  // rpc_ev_loop.cc

  /**
   * @brief Run one iteration of the event loop
   */
  void run_event_loop_one();

  /**
   * @brief Run the event loop forever
   */
  inline void run_event_loop() {
    while (true) {
      run_event_loop_one();
    }
  }

  /**
   * @brief Run the event loop for \p timeout_ms milliseconds
   */
  inline void run_event_loop_timeout(size_t timeout_ms) {
    uint64_t start_tsc = rdtsc();

    while (true) {
      run_event_loop_one();

      double elapsed_ms = to_sec(rdtsc() - start_tsc, nexus->freq_ghz) * 1000;
      if (elapsed_ms > timeout_ms) {
        return;
      }
    }
  }

 private:
  // rpc.cc

  /**
   * @brief Return the hostname and app TID of this Rpc.
   */
  std::string get_name();

  /**
   * @brief Check if fabric port \p fab_port_index is managed by this Rpc
   */
  bool is_fdev_port_managed(size_t fab_port_index);

  /**
   * @brief Process all session management events in the queue and free them.
   * The handlers for individual request/response types should not free packets.
   */
  void handle_session_management();

  /**
   * @brief Generate the start sequence number for a session.
   */
  uint64_t generate_start_seq();

  /**
   * @brief Destroy a session object and remove it from the session list.
   */
  void bury_session(Session *session);

  /**
   * @brief Check if this session pointer is a valid client session.
   */
  bool is_session_ptr_client(Session *session);

  /**
   * @brief Check if this session pointer is a valid server session.
   */
  bool is_session_ptr_server(Session *session);

  // rpc_connect_handlers.cc
  void handle_session_connect_req(SessionMgmtPkt *pkt);
  void handle_session_connect_resp(SessionMgmtPkt *pkt);

  // rpc_disconnect_handlers.cc
  void handle_session_disconnect_req(SessionMgmtPkt *pkt);
  void handle_session_disconnect_resp(SessionMgmtPkt *pkt);

  // rpc_sm_retry.cc
  void send_connect_req_one(Session *session);
  void send_disconnect_req_one(Session *session);
  void add_to_in_flight(Session *session);
  bool is_in_flight(Session *session);
  void remove_from_in_flight(Session *session);

  /**
   * @brief Retry in-flight requests whose retry timeout has expired.
   */
  void retry_in_flight();

  // Constructor args
  Nexus *nexus;
  void *context; /* The application context */
  size_t app_tid;
  session_mgmt_handler_t session_mgmt_handler;
  size_t num_fdev_ports;
  size_t fdev_port_arr[kMaxFabDevPorts];

  // Others
  Transport_ *transport; /* The unreliable transport */

  /*
   * The append-only list of session pointers, indexed by session num.
   * Disconnected sessions are denoted by null pointers. This grows as sessions
   * are repeatedly connected and disconnected, but 8 bytes per session is OK.
   */
  std::vector<Session *> session_vec;

  /*
   * List of client sessions for which session management requests are in
   * flight.
   */
  std::vector<in_flight_req_t> in_flight_vec;

  SessionMgmtHook sm_hook; /* Shared with Nexus for session management */
  SlowRand slow_rand;
};

/* Instantiate required Rpc classes so they get compiled for the linker */
template class Rpc<InfiniBandTransport>;

}  // End ERpc

#endif  // ERPC_RPC_H
