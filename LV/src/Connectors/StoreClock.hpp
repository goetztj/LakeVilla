#pragma once

#include <cstdint>
#include <mutex>

namespace StorageConnector {

// time of the object store (ms since the epoch) and its uncertainty
struct StoreTime {
  int64_t now_ms = 0;
  int64_t uncertainty_ms = 0;

  int64_t earliest() const { return now_ms - uncertainty_ms; }
  int64_t latest() const { return now_ms + uncertainty_ms; }
};

// milliseconds since the epoch of the local clock
int64_t local_time_ms();

// Estimates the offset between the local clock and the store's clock from
// responses that carry the store time with a coarse resolution (HTTP Date
// header: whole seconds). A response with store time D (truncated to the
// resolution), sent at local time s and received at local time r, bounds
// the offset to [D - r, D + resolution - s]. Intersecting these intervals
// narrows the estimate; contradicting samples (clock jumps) restart it.
// Without samples the local clock is used (clocks assumed synchronised).
class OffsetClock {
 public:
  void add_sample(int64_t store_ms, int64_t sent_local_ms,
                  int64_t received_local_ms, int64_t resolution_ms = 1000);

  StoreTime now(int64_t local_ms) const;

  bool has_samples() const;

  // local time of the latest sample (0 if none)
  int64_t last_sample_local_ms() const;

 private:
  mutable std::mutex mutex;
  bool valid = false;
  int64_t low = 0;
  int64_t high = 0;
  int64_t last_sample = 0;
};

}  // namespace StorageConnector
