#include <gtest/gtest.h>

#include <algorithm>

#include "Helpers/Helper.hpp"
#include "LakeVilla/DeltaLog/DeltaLogManager.hpp"
#include "LakeVilla/TransactionManager/Manager.hpp"
#include "test_util.hpp"

using LHExecutor::LHDL::DeltaLogManager;
using LHExecutor::LHDL::DLOperation;

namespace {
std::unique_ptr<rapidjson::Document> parse(const std::string& json) {
  return LHHelpers::readJSON_rapid(json.c_str());
}
}  // namespace

TEST(DeltaLogManager, AddAndRemove) {
  DeltaLogManager log("wh/t/");
  std::string a = "a.parquet", b = "b.parquet";
  log.register_operation(DLOperation::ADD, a);
  log.register_operation(DLOperation::ADD, b);
  log.register_operation(DLOperation::DELETE, a);

  EXPECT_EQ(log.get_list_raw(), std::vector<std::string>{"b.parquet"});
  EXPECT_EQ(log.get_list(), std::vector<std::string>{"wh/t/b.parquet"});
}

// log entries are parsed in parallel and in any order: a remove always wins
TEST(DeltaLogManager, RemoveBeatsAddIndependentOfOrder) {
  DeltaLogManager log("wh/t/");
  std::string a = "a.parquet";
  log.register_operation(DLOperation::DELETE, a);
  log.register_operation(DLOperation::ADD, a);
  EXPECT_TRUE(log.get_list().empty());
}

// overwrite is used by redo/undo steps of a transaction
TEST(DeltaLogManager, OverwriteRestoresFile) {
  DeltaLogManager log("wh/t/");
  std::string a = "a.parquet";
  log.register_operation(DLOperation::ADD, a);
  log.register_operation(DLOperation::DELETE, a);
  log.register_operation(DLOperation::ADD, a, false, true);
  EXPECT_EQ(log.get_list_raw(), std::vector<std::string>{"a.parquet"});
}

TEST(DeltaLogManager, StripsBasePath) {
  DeltaLogManager log("wh/t/");
  std::string full = "wh/t/a.parquet";
  log.register_operation(DLOperation::ADD, full, true);
  EXPECT_EQ(log.get_list_raw(), std::vector<std::string>{"a.parquet"});
}

TEST(DeltaLogManager, FindFileWithStats) {
  DeltaLogManager log("wh/t/");
  for (auto key : {"user1", "user2", "user3"}) {
    auto doc = parse(lvtest::add_action(std::string(key) + ".parquet", 1, key));
    std::string path = std::string(key) + ".parquet";
    log.register_operation(DLOperation::ADD, path, *doc);
  }

  EXPECT_EQ(log.findFileWithStats(0, "user2"), "wh/t/user2.parquet");
  EXPECT_EQ(log.findFileWithStats(0, "user2", false), "user2.parquet");
  EXPECT_EQ(log.findFileWithStats(0, "user9"), "");
}

TEST(DeltaLogManager, RemovedFileIsNotFound) {
  DeltaLogManager log("wh/t/");
  auto doc = parse(lvtest::add_action("k.parquet", 1, "k"));
  std::string path = "k.parquet";
  log.register_operation(DLOperation::ADD, path, *doc);
  log.register_operation(DLOperation::DELETE, path);
  EXPECT_EQ(log.findFileWithStats(0, "k"), "");
}

TEST(DeltaLogManager, Versions) {
  DeltaLogManager log("wh/t/");
  log.register_read_version(5);
  log.register_read_version(3);
  EXPECT_EQ(log.get_read_version(), 5u);
  EXPECT_FALSE(log.register_write_version(5));
  EXPECT_TRUE(log.register_write_version(6));
  EXPECT_EQ(log.get_write_version(), 6u);
}

// non-string/int statistics must not shift the column positions
TEST(DeltaLogManager, StatsKeepColumnPositions) {
  DeltaLogManager log("wh/t/");
  auto doc = parse(
      R"({"add":{"path":"f.parquet","size":1,"stats":"{\"numRecords\":1,\"minValues\":{\"price\":1.5,\"big\":12345678901,\"YCSB_KEY\":\"k\"},\"maxValues\":{\"price\":2.5,\"big\":12345678901,\"YCSB_KEY\":\"k\"}}"}})");
  std::string path = "f.parquet";
  log.register_operation(DLOperation::ADD, path, *doc);
  EXPECT_EQ(log.findFileWithStats(2, "k"), "wh/t/f.parquet");
}

// files without statistics must be skipped, not indexed out of bounds
TEST(DeltaLogManager, FilesWithoutStatsAreSkipped) {
  DeltaLogManager log("wh/t/");
  std::string no_stats = "a.parquet";
  std::vector<std::string> empty;
  log.register_operation(DLOperation::ADD, no_stats, empty, empty);

  auto doc = parse(lvtest::add_action("b.parquet", 1, "k"));
  std::string with_stats = "b.parquet";
  log.register_operation(DLOperation::ADD, with_stats, *doc);

  EXPECT_EQ(log.findFileWithStats(0, "k"), "wh/t/b.parquet");
  EXPECT_EQ(log.findFileWithStats(3, "k"), "");
}

// stats are optional in Delta add actions
TEST(DeltaLogManager, AddWithoutStats) {
  DeltaLogManager log("wh/t/");
  auto doc = parse(R"({"add":{"path":"f.parquet","size":1}})");
  std::string path = "f.parquet";
  log.register_operation(DLOperation::ADD, path, *doc);
  EXPECT_EQ(log.get_list_raw(), std::vector<std::string>{"f.parquet"});
}

// actions of the log (and checkpoints) are applied by version: a file removed
// and added again in a later version (RESTORE) exists, in any parse order
TEST(DeltaLogManager, ActionsApplyByVersion) {
  using LHExecutor::LHDL::DeltaLogEntry;
  auto add = parse(
      R"({"path":"a.parquet","partitionValues":{"p":"1"},"size":5,"modificationTime":7,"tags":{"t":"x"}})");
  auto remove = parse(R"({"path":"a.parquet","deletionTimestamp":9})");
  auto entry = [&](DLOperation op, int64_t version) {
    return DeltaLogEntry::from_action(op, op == DLOperation::ADD ? *add : *remove,
                                      version);
  };

  for (bool reverse : {false, true}) {
    DeltaLogManager log("wh/t/");
    std::vector<std::pair<DLOperation, int64_t>> actions = {
        {DLOperation::ADD, 1}, {DLOperation::DELETE, 2}, {DLOperation::ADD, 3}};
    if (reverse) {
      std::reverse(actions.begin(), actions.end());
    }
    for (auto& [op, version] : actions) {
      log.register_action("a.parquet", entry(op, version));
    }
    auto entries = log.get_entries();
    ASSERT_EQ(entries.size(), 1u) << reverse;
    EXPECT_EQ(entries[0].second.partition_values, R"({"p":"1"})");
    EXPECT_EQ(entries[0].second.tags, R"({"t":"x"})");
    EXPECT_EQ(entries[0].second.modification_time, 7);
    EXPECT_EQ(entries[0].second.size, 5);
  }

  // a remove wins over an add of the same version; the tombstone keeps the
  // size and partition values of the add
  DeltaLogManager log("wh/t/");
  log.register_action("a.parquet", entry(DLOperation::DELETE, 4));
  log.register_action("a.parquet", entry(DLOperation::ADD, 4));
  EXPECT_TRUE(log.get_entries().empty());
  log.register_action("b.parquet", entry(DLOperation::ADD, 1));
  log.register_action("b.parquet", entry(DLOperation::DELETE, 2));
  auto tombstones = log.get_tombstones();
  ASSERT_EQ(tombstones.size(), 2u);
  for (auto& [path, tombstone] : tombstones) {
    if (path == "b.parquet") {
      EXPECT_EQ(tombstone.size, 5);
      EXPECT_EQ(tombstone.partition_values, R"({"p":"1"})");
      EXPECT_EQ(tombstone.deletion_timestamp, 9);
    }
  }
}

TEST(DeltaLogManager, TransactionsAndDomains) {
  DeltaLogManager log("wh/t/");
  log.set_transaction("app", 5, 50, 3);
  log.set_transaction("app", 4, 40, 2);  // older version
  log.set_domain_metadata("d", std::string("{}"), 1);
  log.set_domain_metadata("d", std::nullopt, 2);
  log.set_domain_metadata("e", std::string("{\"x\":1}"), 2);

  auto txns = log.get_transactions();
  ASSERT_EQ(txns.size(), 1u);
  EXPECT_EQ(txns[0].version, 5);
  EXPECT_EQ(txns[0].last_updated, 50);
  auto domains = log.get_domain_metadata();
  ASSERT_EQ(domains.size(), 1u);
  EXPECT_EQ(domains[0].domain, "e");
}

TEST(GlobalRelation, Dependencies) {
  LHTransactions::GlobalRelation relation;
  EXPECT_FALSE(relation.register_dependency(7));
  EXPECT_TRUE(relation.register_dependency(7));
  EXPECT_FALSE(relation.register_dependency(8));
  auto deps = relation.get_dependencies();
  std::sort(deps.begin(), deps.end());
  EXPECT_EQ(deps, (std::vector<uint32_t>{7, 8}));

  // a transaction we depend on cannot also precede us
  EXPECT_FALSE(relation.register_precedence(7));
  EXPECT_TRUE(relation.register_precedence(9));
}
