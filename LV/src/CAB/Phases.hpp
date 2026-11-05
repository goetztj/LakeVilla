#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

namespace LHCAB {

// Phase switches of a benchmark run, shared by the driver and all client
// threads (atomics: plain bools in spin loops are data races and may be
// optimised into endless loops).
struct Phases {
  explicit Phases(size_t phases) : triggers(phases) {}

  std::vector<std::atomic<bool>> triggers;
  std::atomic<bool> end{false};

  size_t size() const { return triggers.size(); }

  bool started(size_t phase) const { return triggers[phase].load(); }

  void start(size_t phase) { triggers[phase] = true; }

  // blocks until the first phase starts (or the run ends)
  void wait_for_start() const {
    while (!triggers[0] && !end) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
};

}  // namespace LHCAB
