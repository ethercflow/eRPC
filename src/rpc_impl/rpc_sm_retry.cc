/**
 * @file rpc_sm_retry.cc
 * @brief Methods to send/resend session management requests.
 */

#include <algorithm>

#include "rpc.h"
#include "util/udp_client.h"

namespace ERpc {

template <class Transport_>
void Rpc<Transport_>::send_connect_req_one(Session *session) {
  assert(session != nullptr && session->is_client());
  assert(session->state == SessionState::kConnectInProgress);

  SessionMgmtPkt connect_req(SessionMgmtPktType::kConnectReq);
  connect_req.client = session->client;
  connect_req.server = session->server;
  connect_req.send_to(session->server.hostname, &nexus->udp_config);
}

template <class Transport_>
void Rpc<Transport_>::send_disconnect_req_one(Session *session) {
  assert(session != nullptr && session->is_client());
  assert(session->state == SessionState::kDisconnectInProgress);

  SessionMgmtPkt connect_req(SessionMgmtPktType::kDisconnectReq);
  connect_req.client = session->client;
  connect_req.server = session->server;
  connect_req.send_to(session->server.hostname, &nexus->udp_config);
}

template <class Transport_>
bool Rpc<Transport_>::mgmt_retry_queue_contains(Session *session) {
  return std::find(mgmt_retry_queue.begin(), mgmt_retry_queue.end(), session) !=
         mgmt_retry_queue.end();
}

template <class Transport_>
void Rpc<Transport_>::mgmt_retry_queue_add(Session *session) {
  assert(session != nullptr && session->is_client());

  /* Only client-mode sessions can be in the management retry queue */
  assert(session->is_client());

  /* Ensure that we don't have an in-flight management req for this session */
  assert(!mgmt_retry_queue_contains(session));

  session->client_info.mgmt_req_tsc = rdtsc(); /* Save tsc for retry */
  mgmt_retry_queue.push_back(session);
}

template <class Transport_>
void Rpc<Transport_>::mgmt_retry_queue_remove(Session *session) {
  assert(session != nullptr && session->is_client());
  assert(mgmt_retry_queue_contains(session));

  size_t initial_size = mgmt_retry_queue.size(); /* Debug-only */
  _unused(initial_size);

  mgmt_retry_queue.erase(
      std::remove(mgmt_retry_queue.begin(), mgmt_retry_queue.end(), session),
      mgmt_retry_queue.end());

  assert(mgmt_retry_queue.size() == initial_size - 1);
}

template <class Transport_>
void Rpc<Transport_>::mgmt_retry() {
  assert(mgmt_retry_queue.size() > 0);
  uint64_t cur_tsc = rdtsc();

  for (Session *session : mgmt_retry_queue) {
    assert(session != nullptr);
    SessionState state = session->state;
    assert(state == SessionState::kConnectInProgress ||
           state == SessionState::kDisconnectInProgress);

    uint64_t elapsed_cycles = cur_tsc - session->client_info.mgmt_req_tsc;
    assert(elapsed_cycles > 0);

    double elapsed_ms = to_sec(elapsed_cycles, nexus->freq_ghz) * 1000;
    if (elapsed_ms > kSessionMgmtRetransMs) {
      /* We need to retransmit */
      switch (state) {
        case SessionState::kConnectInProgress:
          erpc_dprintf(
              "eRPC Rpc %s: Retrying session connect req for session %u.\n",
              get_name().c_str(), session->client.session_num);

          send_connect_req_one(session);
          break; /* Process other in-flight requests */
        case SessionState::kDisconnectInProgress:
          erpc_dprintf(
              "eRPC Rpc %s: Retrying session disconnect req for session %u.\n",
              get_name().c_str(), session->client.session_num);

          send_disconnect_req_one(session);
          break; /* Process other in-flight requests */
        default:
          assert(false);
          exit(-1);
      }

      session->client_info.mgmt_req_tsc = rdtsc(); /* Update for retry */
    }
  }
}

}  // End ERpc
