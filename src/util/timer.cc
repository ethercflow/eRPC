#include "timer.h"

namespace erpc {

size_t rdtsc() {
  uint64_t rax;
  uint64_t rdx;
  asm volatile("rdtsc" : "=a"(rax), "=d"(rdx));
  return static_cast<size_t>((rdx << 32) | rax);
}

double to_usec(size_t cycles, double freq_ghz) {
  return (cycles / (freq_ghz * 1000));
}

}
