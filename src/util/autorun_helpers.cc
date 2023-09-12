#include "autorun_helpers.h"

namespace erpc {

std::string get_uri_for_process(size_t process_i) {
  std::string hostname = erpc::get_hostname_for_process(process_i);
  std::string udp_port_str = erpc::get_udp_port_for_process(process_i);
  return hostname + ":" + udp_port_str;
}

}
