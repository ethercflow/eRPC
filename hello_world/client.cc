#include "common.h"
erpc::Rpc *rpc;
erpc::MsgBuffer req;
erpc::MsgBuffer resp;

void cont_func(void *, void *) { printf("%s\n", resp.buf_); }

void sm_handler(int, erpc::SmEventType, erpc::SmErrType, void *) {}

int main() {
  std::string client_uri = kClientHostname + ":" + std::to_string(kUDPPort);
  erpc::Nexus nexus(client_uri);

  rpc = new erpc::Rpc(&nexus, nullptr, 0, reinterpret_cast<void *>(sm_handler));

  std::string server_uri = kServerHostname + ":" + std::to_string(kUDPPort);
  int session_num = rpc->create_session(server_uri, 0);

  while (!rpc->is_connected(session_num)) rpc->run_event_loop_once();

  req = rpc->alloc_msg_buffer_or_die(kMsgSize);
  resp = rpc->alloc_msg_buffer_or_die(kMsgSize);

  rpc->enqueue_request(session_num, kReqType, &req, &resp, reinterpret_cast<void *>(cont_func), nullptr);
  rpc->run_event_loop(100);

  delete rpc;
}
