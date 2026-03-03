#include <chrono>

namespace LHTimer {
struct Timer {
  std::chrono::time_point<std::chrono::high_resolution_clock> timestamp;

  Timer();

  ~Timer() = default;

  void start();

  double end();
};
}  // namespace LHTimer