#include "StoreClock.hpp"

#include <algorithm>
#include <chrono>

int64_t StorageConnector::local_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void StorageConnector::OffsetClock::add_sample(int64_t store_ms,
                                               int64_t sent_local_ms,
                                               int64_t received_local_ms,
                                               int64_t resolution_ms) {
  int64_t sample_low = store_ms - received_local_ms;
  int64_t sample_high = store_ms + resolution_ms - sent_local_ms;

  std::lock_guard<std::mutex> guard(this->mutex);
  this->last_sample = received_local_ms;
  if (this->valid && sample_low <= this->high && sample_high >= this->low) {
    this->low = std::max(this->low, sample_low);
    this->high = std::min(this->high, sample_high);
  } else {
    // first sample or a clock jump: start over
    this->low = sample_low;
    this->high = sample_high;
    this->valid = true;
  }
}

StorageConnector::StoreTime StorageConnector::OffsetClock::now(
    int64_t local_ms) const {
  std::lock_guard<std::mutex> guard(this->mutex);
  if (!this->valid) {
    return {local_ms, 0};
  }
  int64_t offset = this->low + (this->high - this->low) / 2;
  return {local_ms + offset, (this->high - this->low + 1) / 2};
}

bool StorageConnector::OffsetClock::has_samples() const {
  std::lock_guard<std::mutex> guard(this->mutex);
  return this->valid;
}

int64_t StorageConnector::OffsetClock::last_sample_local_ms() const {
  std::lock_guard<std::mutex> guard(this->mutex);
  return this->last_sample;
}
