#include "Timer.hpp"

LHTimer::Timer::Timer() { this->start(); }

void LHTimer::Timer::start() {
  timestamp =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

double LHTimer::Timer::end() {
  auto end_timestamp = std::chrono::high_resolution_clock::now();
  std::chrono::high_resolution_clock::time_point start{
      std::chrono::high_resolution_clock::duration{timestamp.load()}};
  const std::chrono::duration<double, std::milli> fp_ms =
      end_timestamp - start;
  return fp_ms.count();
}
