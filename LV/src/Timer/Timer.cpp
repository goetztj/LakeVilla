#include "Timer.hpp"

LHTimer::Timer::Timer() {
  timestamp = std::chrono::high_resolution_clock::now();
}

void LHTimer::Timer::start() {
  timestamp = std::chrono::high_resolution_clock::now();
}

double LHTimer::Timer::end() {
  auto end_timestamp = std::chrono::high_resolution_clock::now();
  const std::chrono::duration<double, std::milli> fp_ms =
      end_timestamp - timestamp;
  return fp_ms.count();
}