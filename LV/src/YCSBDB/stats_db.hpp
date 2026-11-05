#pragma once

namespace ycsbc {

// A YCSB DB that collects latency statistics. Drivers set defer_stats to
// print them after the timed region instead of in Close().
struct StatsDB {
  bool defer_stats = false;

  virtual void print_stats() = 0;

  virtual ~StatsDB() = default;
};

}  // namespace ycsbc
