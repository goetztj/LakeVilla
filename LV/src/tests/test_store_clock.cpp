#include <gtest/gtest.h>

#include "Connectors/MemoryConnector.hpp"
#include "Connectors/StoreClock.hpp"

using StorageConnector::OffsetClock;

TEST(OffsetClock, WithoutSamplesUsesLocalClock) {
  OffsetClock clock;
  auto t = clock.now(5000);
  EXPECT_EQ(t.now_ms, 5000);
  EXPECT_EQ(t.uncertainty_ms, 0);
  EXPECT_FALSE(clock.has_samples());
}

TEST(OffsetClock, SingleSampleBoundsOffset) {
  OffsetClock clock;
  // store is 10 s ahead; Date header 1010 s (truncated), request 1000.3..1000.4
  clock.add_sample(1010000, 1000300, 1000400);
  auto t = clock.now(1000400);
  // true store time at local 1000400 is in [1010000, 1011000 + 100]
  EXPECT_GE(t.latest(), 1010000);
  EXPECT_LE(t.earliest(), 1010400);
  EXPECT_LE(t.uncertainty_ms, 600);
}

TEST(OffsetClock, SecondBoundariesNarrowTheEstimate) {
  OffsetClock clock;
  const int64_t offset = 7300;  // store ahead by 7.3 s
  // samples every 70 ms with 5 ms round trips
  for (int64_t local = 100000; local < 103000; local += 70) {
    int64_t store = local + 2 + offset;  // request handled mid-flight
    clock.add_sample(store - store % 1000, local, local + 5);
  }
  auto t = clock.now(200000);
  EXPECT_LE(t.uncertainty_ms, 50);
  EXPECT_LE(t.earliest(), 200000 + offset);
  EXPECT_GE(t.latest(), 200000 + offset);
}

TEST(OffsetClock, ClockJumpRestarts) {
  OffsetClock clock;
  clock.add_sample(1000000, 1000000, 1000010);
  // store clock jumped by an hour: contradicting interval
  clock.add_sample(4600000, 1000500, 1000510);
  auto t = clock.now(1000510);
  EXPECT_GE(t.latest(), 4600000);
  EXPECT_LE(t.earliest(), 4601000);
}

TEST(MemoryConnector, InjectableClock) {
  StorageConnector::MemoryConnector conn;
  int64_t now = 42000;
  conn.clock = [&now] { return now; };
  EXPECT_EQ(conn.store_time().now_ms, 42000);
  std::string data = "x";
  conn.write("k", data.data(), data.size());
  now = 50000;
  StorageConnector::ObjectVersion version;
  conn.read_versioned("k", version);
  EXPECT_EQ(version.last_modified_ms, 42000);
  EXPECT_EQ(conn.store_time().now_ms, 50000);
}
