#pragma once
#include <atomic>
#include <chrono>

namespace LHTimer {
// start() and end() may be called from different threads (freshness
// benchmark: the writer starts, the reader stops)
struct Timer {
  std::atomic<std::chrono::high_resolution_clock::rep> timestamp;

  Timer();

  ~Timer() = default;

  void start();

  double end();
};
}  // namespace LHTimer
