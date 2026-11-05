// Protocol tests of the TransactionManager on top of the in-memory store.
// Each test creates a small YCSB-like Delta table (one row per data file,
// statistics on YCSB_KEY) and runs transactions against it.
#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <set>

#include "Helpers/Helper.hpp"
#include "LakeVilla/TransactionManager/ManagerDeltaLake.hpp"
#include "test_util.hpp"

using LHTransactions::LakeVillaConf;
using LHTransactions::TransactionManagerDeltaLake;
using StorageConnector::MemoryConnector;

namespace {

const std::string BASE = "wh/ycsb.db/usertable/";
const std::vector<std::string> KEYS = {"user0", "user1", "user2", "user3",
                                       "user4"};

LakeVillaConf parse_levels(const std::string& levels) {
  LakeVillaConf conf = {false, false, false};
  for (char c : levels) {
    conf[c - '0'] = true;
  }
  return conf;
}

std::string version_path(const std::string& base, uint64_t version) {
  return base + "_delta_log/" + lvtest::log_name(version);
}

// the actions (JSON lines) of a log version, without commitInfo
std::vector<std::string> actions_of(MemoryConnector& conn,
                                    const std::string& base,
                                    uint64_t version) {
  auto content = conn.get(version_path(base, version));
  std::vector<std::string> actions;
  if (!content) {
    return actions;
  }
  for (auto& line : lvtest::split_lines(*content)) {
    if (line.find("\"commitInfo\"") == std::string::npos) {
      actions.push_back(line);
    }
  }
  return actions;
}

std::string field0_of(const std::shared_ptr<arrow::Table>& table,
                      const std::string& key) {
  auto combined = table->CombineChunks().ValueOrDie();
  auto key_col = std::static_pointer_cast<arrow::StringArray>(
      combined->GetColumnByName("YCSB_KEY")->chunk(0));
  auto field_col = std::static_pointer_cast<arrow::StringArray>(
      combined->GetColumnByName("field0")->chunk(0));
  for (int64_t i = 0; i < key_col->length(); i++) {
    if (key_col->GetString(i) == key) {
      return field_col->GetString(i);
    }
  }
  return "";
}

class TransactionTest : public ::testing::TestWithParam<std::string> {
 protected:
  MemoryConnector conn;
  std::string base = BASE;

  void SetUp() override { lvtest::create_ycsb_table(conn, base, KEYS); }

  std::unique_ptr<TransactionManagerDeltaLake> manager(uint32_t txn_id) {
    return std::make_unique<TransactionManagerDeltaLake>(
        parse_levels(GetParam()), base, &conn, txn_id);
  }

  bool uses_markers() {
    auto levels = parse_levels(GetParam());
    return levels[0] || levels[1];
  }

  std::vector<std::string> current_keys() {
    auto tm = manager(999);
    EXPECT_TRUE(tm->begin_transaction_ycsb());
    auto keys = lvtest::keys_of(tm->read_table(0));
    tm->commit(true);
    return keys;
  }

  std::shared_ptr<arrow::Table> current_table() {
    auto tm = manager(998);
    EXPECT_TRUE(tm->begin_transaction_ycsb());
    auto table = tm->read_table(0);
    tm->commit(true);
    return table;
  }

  bool insert(TransactionManagerDeltaLake& tm, const std::string& key) {
    return tm.add_file(0, lvtest::ycsb_table(key), key);
  }

  bool update(TransactionManagerDeltaLake& tm, const std::string& key,
              const std::string& value) {
    std::string path;
    auto old = tm.read_file_as_table(0, key, path);
    if (!old) {
      return false;
    }
    std::vector<std::pair<std::string, std::string>> values = {
        {"field0", value}};
    auto prev = tm.register_update_pairs(
        values, LHTransactions::UpdateOperations::REP, values);
    tm.remove_file(0, key, &path, prev);
    return tm.add_file(0, lvtest::ycsb_table(key, value), key, prev);
  }
};

}  // namespace

TEST_P(TransactionTest, ReadTable) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  EXPECT_EQ(lvtest::keys_of(tm->read_table(0)), KEYS);
  EXPECT_TRUE(tm->commit(true));
}

TEST_P(TransactionTest, ReadFileByKey) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());

  std::vector<std::pair<std::string, std::string>> result;
  std::string path;
  auto found = tm->read_file(0, "user3", nullptr, result, path);
  EXPECT_EQ(found, base + "part-00003-test.snappy.parquet");
  ASSERT_EQ(result.size(), 11u);
  EXPECT_EQ(result[0].first, "YCSB_KEY");
  EXPECT_NE(result[0].second.find("user3"), std::string::npos);

  result.clear();
  EXPECT_EQ(tm->read_file(0, "unknown", nullptr, result, path), "");
  tm->commit(true);
}

TEST_P(TransactionTest, InsertCommit) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  EXPECT_TRUE(tm->commit(false));
  tm = nullptr;

  auto expected = KEYS;
  expected.push_back("user9");
  EXPECT_EQ(current_keys(), expected);

  auto actions = actions_of(conn, base, 1);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_NE(actions[0].find("\"add\""), std::string::npos);
  EXPECT_EQ(actions[0].find("\"marker\""), std::string::npos);
}

TEST_P(TransactionTest, DeleteCommit) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(tm->remove_file(0, std::string("user1")));
  EXPECT_TRUE(tm->commit(false));
  tm = nullptr;

  EXPECT_EQ(current_keys(),
            (std::vector<std::string>{"user0", "user2", "user3", "user4"}));

  auto actions = actions_of(conn, base, 1);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_NE(actions[0].find(R"("remove":{"path":"part-00001-test.snappy.parquet")"),
            std::string::npos);
}

TEST_P(TransactionTest, UpdateCommit) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(update(*tm, "user2", "updated"));
  EXPECT_TRUE(tm->commit(false));
  tm = nullptr;

  EXPECT_EQ(current_keys(), KEYS);
  EXPECT_EQ(field0_of(current_table(), "user2"), "updated");
  EXPECT_EQ(field0_of(current_table(), "user3"), "v");
}

TEST_P(TransactionTest, ReadOnlyCommitDoesNotChangeData) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  tm->read_table(0);
  EXPECT_TRUE(tm->commit(true));
  tm = nullptr;

  EXPECT_EQ(current_keys(), KEYS);
  // a reserved version (marker) is released with an empty commit
  EXPECT_TRUE(actions_of(conn, base, 1).empty());
}

TEST_P(TransactionTest, MarkerIsPlacedAndReplacedByCommit) {
  if (!uses_markers()) {
    GTEST_SKIP() << "no markers without level 0/1";
  }
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());

  auto marker = conn.get(version_path(base, 1));
  ASSERT_TRUE(marker.has_value());
  EXPECT_NE(marker->find("\"marker\""), std::string::npos);
  EXPECT_NE(marker->find("\"running\":true"), std::string::npos);

  ASSERT_TRUE(insert(*tm, "user9"));
  ASSERT_TRUE(tm->commit(false));
  EXPECT_EQ(conn.get(version_path(base, 1))->find("\"marker\""),
            std::string::npos);
}

TEST_P(TransactionTest, ConcurrentWriters) {
  auto tm1 = manager(1);
  auto tm2 = manager(2);
  ASSERT_TRUE(tm1->begin_transaction_ycsb());
  ASSERT_TRUE(tm2->begin_transaction_ycsb());

  ASSERT_TRUE(insert(*tm1, "user8"));
  ASSERT_TRUE(insert(*tm2, "user9"));

  EXPECT_TRUE(tm1->commit(false));
  bool second = tm2->commit(false);
  tm1 = nullptr;
  tm2 = nullptr;

  auto expected = KEYS;
  expected.push_back("user8");
  if (uses_markers()) {
    // markers reserve distinct versions: both writers succeed
    EXPECT_TRUE(second);
    expected.push_back("user9");
  } else {
    // both read version 0 and try to write version 1: one has to abort
    EXPECT_FALSE(second);
  }
  EXPECT_EQ(current_keys(), expected);
}

TEST_P(TransactionTest, GlobalLogIsWritten) {
  if (!parse_levels(GetParam())[2]) {
    GTEST_SKIP() << "global log only with level 2";
  }
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  ASSERT_TRUE(tm->commit(false));

  auto global = conn.get(LV_DEFAULT_GLOBAL_LOG);
  ASSERT_TRUE(global.has_value());
  EXPECT_EQ(*global, base + " 1\n");
  EXPECT_FALSE(conn.check(LV_DEFAULT_GLOBAL_TMP_LOG));
}

TEST_P(TransactionTest, ConfiguredGlobalLogPath) {
  if (!parse_levels(GetParam())[2]) {
    GTEST_SKIP() << "global log only with level 2";
  }
  auto tm = manager(1);
  tm->set_global_log_paths("other/log.txt", "other/log.txt.tmp");
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  ASSERT_TRUE(tm->commit(false));
  EXPECT_TRUE(conn.check("other/log.txt"));
  EXPECT_FALSE(conn.check(LV_DEFAULT_GLOBAL_LOG));
}

TEST_P(TransactionTest, SublogsAreCleanedUp) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  ASSERT_TRUE(tm->commit(false));
  for (auto& key : conn.keys(base + "_delta_log/")) {
    EXPECT_EQ(key.find(".sub"), std::string::npos) << key;
  }
}

// tables with a (Spark-written) checkpoint
TEST_P(TransactionTest, TableWithCheckpoint) {
  // versions 1..2 add user5/user6, a checkpoint at version 2 contains all
  for (int v = 1; v <= 2; v++) {
    auto key = "user" + std::to_string(4 + v);
    auto file = "part-1000" + std::to_string(v) + "-test.snappy.parquet";
    auto content = lvtest::ycsb_parquet(key);
    conn.put(base + file, content);
    lvtest::write_version(conn, base, v,
                          {lvtest::add_action(file, content.size(), key)});
  }
  // a minimal Delta checkpoint: add.path and add.stats
  {
    std::vector<std::string> paths, stats;
    auto keys = KEYS;
    keys.push_back("user5");
    keys.push_back("user6");
    for (size_t i = 0; i < keys.size(); i++) {
      auto doc = LHHelpers::readJSON_rapid(
          lvtest::add_action(i < 5 ? "part-0000" + std::to_string(i) +
                                         "-test.snappy.parquet"
                                   : "part-1000" + std::to_string(i - 4) +
                                         "-test.snappy.parquet",
                             1, keys[i])
              .c_str());
      paths.push_back((*doc)["add"]["path"].GetString());
      stats.push_back((*doc)["add"]["stats"].GetString());
    }
    arrow::StringBuilder path_builder, stats_builder;
    ASSERT_TRUE(path_builder.AppendValues(paths).ok());
    ASSERT_TRUE(stats_builder.AppendValues(stats).ok());
    std::shared_ptr<arrow::Array> path_array, stats_array;
    ASSERT_TRUE(path_builder.Finish(&path_array).ok());
    ASSERT_TRUE(stats_builder.Finish(&stats_array).ok());
    auto add_type = arrow::struct_({arrow::field("path", arrow::utf8()),
                                    arrow::field("stats", arrow::utf8())});
    auto add = std::make_shared<arrow::StructArray>(
        add_type, paths.size(),
        std::vector<std::shared_ptr<arrow::Array>>{path_array, stats_array});
    auto txn = *arrow::MakeArrayOfNull(arrow::utf8(), paths.size());
    auto table = arrow::Table::Make(
        arrow::schema({arrow::field("txn", arrow::utf8()),
                       arrow::field("add", add_type)}),
        {txn, add});
    conn.put(base + "_delta_log/00000000000000000002.checkpoint.parquet",
             LHHelpers::encodeAsParquet_str(table));
  }
  conn.put(base + "_delta_log/_last_checkpoint", R"({"version":2,"size":7})");
  // a log cleanup may have removed versions before the checkpoint
  conn.delete_file(version_path(base, 1));

  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  auto keys = lvtest::keys_of(tm->read_table(0));
  EXPECT_EQ(keys.size(), 7u);

  std::vector<std::pair<std::string, std::string>> result;
  std::string path;
  EXPECT_NE(tm->read_file(0, "user6", nullptr, result, path), "");
  tm->commit(true);
}

// empty tables must not divide by zero
TEST_P(TransactionTest, EmptyTable) {
  std::string empty = "wh/empty.db/t/";
  lvtest::create_ycsb_table(conn, empty, {});
  TransactionManagerDeltaLake tm(parse_levels(GetParam()), empty, &conn, 1);
  ASSERT_TRUE(tm.begin_transaction_ycsb());
  auto table = tm.read_table(0);
  EXPECT_TRUE(table == nullptr || table->num_rows() == 0);
  tm.commit(true);
}

// a path without a Delta log is not a table
TEST_P(TransactionTest, MissingTableFailsToBegin) {
  std::string missing = "wh/missing.db/t/";
  TransactionManagerDeltaLake tm(parse_levels(GetParam()), missing, &conn, 1);
  EXPECT_FALSE(tm.begin_transaction_ycsb());
  // failed attempts leave nothing behind
  EXPECT_TRUE(conn.keys(missing).empty());
}

// a failed read must be reported, not terminate the process
TEST_P(TransactionTest, FailedReadIsReported) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  conn.fail_hook = [](MemoryConnector::Op op, const std::string& path) {
    return op == MemoryConnector::Op::Read &&
           path.find(".parquet") != std::string::npos;
  };
  std::vector<std::pair<std::string, std::string>> result;
  std::string path;
  EXPECT_EQ(tm->read_file(0, "user1", nullptr, result, path), "");
  conn.fail_hook = nullptr;
  tm->commit(true);
}

// a failed data-file write must be reported, not terminate the process
TEST_P(TransactionTest, FailedWriteIsReported) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  conn.fail_hook = [](MemoryConnector::Op op, const std::string& path) {
    return op == MemoryConnector::Op::Write &&
           path.find("lhdata") != std::string::npos;
  };
  EXPECT_FALSE(insert(*tm, "user9"));
  conn.fail_hook = nullptr;
  tm->commit(true);
}

// read_table must survive unreadable data files
TEST_P(TransactionTest, ReadTableWithMissingFile) {
  conn.delete_file(base + "part-00002-test.snappy.parquet");
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  EXPECT_EQ(tm->read_table(0), nullptr);
  tm->commit(true);
}

// read_random_file_as_table returns the file on success
TEST_P(TransactionTest, ReadRandomFileAsTable) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  std::string path;
  auto table = tm->read_random_file_as_table(0, path);
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->num_rows(), 1);
  tm->commit(true);
}

// commit(data_read, data_write) must commit, not abort
TEST_P(TransactionTest, CommitWithStatistics) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  double read = 0, written = 0;
  EXPECT_TRUE(tm->commit(read, written, /*single_threaded=*/true));
  EXPECT_GT(written, 0);
  tm = nullptr;
  EXPECT_EQ(current_keys().size(), KEYS.size() + 1);
}

// removes in a second table with a base path of different length
TEST_P(TransactionTest, MultiTableRemovePaths) {
  std::string other = "wh/a_much_longer_name.db/accounts/";
  lvtest::create_ycsb_table(conn, other, {"acc0", "acc1"});

  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(tm->open_new_table(other));
  auto other_id = tm->get_table_id(other);
  ASSERT_EQ(other_id, 1u);
  ASSERT_TRUE(tm->remove_file(other_id, std::string("acc1")));
  ASSERT_TRUE(tm->commit(false));

  auto actions = actions_of(conn, other, 1);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_NE(actions[0].find(R"("path":"part-00001-test.snappy.parquet")"),
            std::string::npos)
      << actions[0];
}

// a commit whose log file cannot be written must fail
TEST_P(TransactionTest, FailedCommitWriteIsReported) {
  auto tm = manager(1);
  tm->set_storage_retries(2);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  std::string v1 = version_path(base, 1);
  conn.fail_hook = [v1](MemoryConnector::Op op, const std::string& path) {
    return (op == MemoryConnector::Op::Write ||
            op == MemoryConnector::Op::WriteAtomic ||
            op == MemoryConnector::Op::WriteIfMatch) &&
           path == v1;
  };
  EXPECT_FALSE(tm->commit(false));
  conn.fail_hook = nullptr;
}

// an unreadable log entry must not block forever
TEST_P(TransactionTest, UnreadableLogFailsToBegin) {
  std::string v0 = version_path(base, 0);
  conn.fail_hook = [v0](MemoryConnector::Op op, const std::string& path) {
    return op == MemoryConnector::Op::Read && path == v0;
  };
  auto tm = manager(1);
  tm->set_storage_retries(2);
  EXPECT_FALSE(tm->begin_transaction_ycsb());
  conn.fail_hook = nullptr;
}

// committed add/remove actions follow the Delta protocol
TEST_P(TransactionTest, CommitFollowsDeltaProtocol) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(update(*tm, "user2", "updated"));
  ASSERT_TRUE(tm->commit(false));

  auto content = conn.get(version_path(base, 1));
  ASSERT_TRUE(content.has_value());
  bool seen_add = false, seen_remove = false;
  for (auto& line : lvtest::split_lines(*content)) {
    auto doc = LHHelpers::readJSON_rapid(line.c_str());
    ASSERT_TRUE(doc->IsObject()) << line;
    if (doc->HasMember("commitInfo")) {
      EXPECT_GT((*doc)["commitInfo"]["timestamp"].GetInt64(), 0);
    }
    if (doc->HasMember("add")) {
      seen_add = true;
      auto& add = (*doc)["add"];
      EXPECT_TRUE(add["partitionValues"].IsObject());
      EXPECT_GT(add["modificationTime"].GetInt64(), 0);
      auto stored = conn.get(base + add["path"].GetString());
      ASSERT_TRUE(stored.has_value());
      EXPECT_EQ(add["size"].GetUint64(), stored->size());
      auto stats = LHHelpers::readJSON_rapid(add["stats"].GetString());
      EXPECT_EQ((*stats)["numRecords"].GetInt64(), 1);
    }
    if (doc->HasMember("remove")) {
      seen_remove = true;
      auto& remove = (*doc)["remove"];
      EXPECT_TRUE(remove["partitionValues"].IsObject());
      EXPECT_GT(remove["deletionTimestamp"].GetInt64(), 0);
      if (remove.HasMember("size")) {
        EXPECT_GT(remove["size"].GetInt64(), 0);
        EXPECT_LT(remove["size"].GetInt64(), int64_t(UINT32_MAX));
      }
    }
  }
  EXPECT_TRUE(seen_add);
  EXPECT_TRUE(seen_remove);
}

// without markers, a commit must never overwrite a version that another
// writer (e.g. Spark) created concurrently
TEST_P(TransactionTest, CommitDoesNotOverwriteForeignVersion) {
  if (uses_markers()) {
    GTEST_SKIP() << "with markers the version is reserved upfront";
  }
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));

  std::string v1 = version_path(base, 1);
  std::string foreign = R"({"commitInfo":{"engineInfo":"Spark"}})";
  bool injected = false;
  conn.fail_hook = [&](MemoryConnector::Op op, const std::string& path) {
    if (!injected && path == v1 &&
        (op == MemoryConnector::Op::Write ||
         op == MemoryConnector::Op::WriteAtomic)) {
      injected = true;
      conn.put(v1, foreign);
    }
    return false;
  };
  EXPECT_FALSE(tm->commit(false));
  conn.fail_hook = nullptr;
  EXPECT_EQ(*conn.get(v1), foreign);
}

// a lock file left behind by a crashed client must not block forever
TEST_P(TransactionTest, StaleGlobalLockIsBroken) {
  if (!parse_levels(GetParam())[2]) {
    GTEST_SKIP() << "global log only with level 2";
  }
  conn.put(LV_DEFAULT_GLOBAL_TMP_LOG, "crashed-client");

  auto tm = manager(1);
  tm->set_global_lock_timing(/*lease_ms=*/100, /*timeout_ms=*/10000);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  EXPECT_TRUE(tm->commit(false));
  EXPECT_FALSE(conn.check(LV_DEFAULT_GLOBAL_TMP_LOG));
}

// an active lock holder is not interrupted; the waiter gives up
TEST_P(TransactionTest, GlobalLockTimeout) {
  if (!parse_levels(GetParam())[2]) {
    GTEST_SKIP() << "global log only with level 2";
  }
  conn.put(LV_DEFAULT_GLOBAL_TMP_LOG, "active-client-0");
  int renewals = 0;
  // the holder keeps working: the lock content changes over time
  conn.fail_hook = [&](MemoryConnector::Op op, const std::string& path) {
    if (op == MemoryConnector::Op::Read && path == LV_DEFAULT_GLOBAL_TMP_LOG) {
      conn.put(path, "active-client-" + std::to_string(++renewals));
    }
    return false;
  };

  auto tm = manager(1);
  tm->set_global_lock_timing(/*lease_ms=*/10000, /*timeout_ms=*/200);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  EXPECT_FALSE(tm->commit(false));
  conn.fail_hook = nullptr;

  // the other client's lock is untouched and nothing was committed
  EXPECT_TRUE(conn.check(LV_DEFAULT_GLOBAL_TMP_LOG));
  EXPECT_EQ(current_keys(), KEYS);
}

// statistics describe the actual file content
TEST_P(TransactionTest, StatisticsMatchContent) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(tm->add_file(0, lvtest::ycsb_table("user9", "abc"), "user9"));
  ASSERT_TRUE(tm->commit(false));

  auto actions = actions_of(conn, base, 1);
  ASSERT_EQ(actions.size(), 1u);
  auto doc = LHHelpers::readJSON_rapid(actions[0].c_str());
  auto stats = LHHelpers::readJSON_rapid((*doc)["add"]["stats"].GetString());
  EXPECT_EQ((*stats)["numRecords"].GetInt64(), 1);
  EXPECT_STREQ((*stats)["minValues"]["YCSB_KEY"].GetString(), "user9");
  EXPECT_STREQ((*stats)["minValues"]["field0"].GetString(), "abc");
  EXPECT_STREQ((*stats)["maxValues"]["field9"].GetString(), "abc");
  EXPECT_EQ((*stats)["nullCount"]["field3"].GetInt64(), 0);
}

// files of other engines (arbitrary names) can be removed
TEST_P(TransactionTest, RemoveFileWithForeignName) {
  std::string foreign = "20260101_000000_00000_abcde-0f1e2d3c.parquet";
  auto content = lvtest::ycsb_parquet("user7");
  conn.put(base + foreign, content);
  lvtest::write_version(conn, base, 1,
                        {lvtest::add_action(foreign, content.size(), "user7")});

  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(tm->remove_file(0, std::string("user7")));
  ASSERT_TRUE(tm->commit(false));
  tm = nullptr;

  auto actions = actions_of(conn, base, 2);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_NE(actions[0].find(foreign), std::string::npos);
  EXPECT_EQ(current_keys(), KEYS);
}

TEST_P(TransactionTest, RemoveUnknownKeyFails) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  std::string path = "unchanged";
  EXPECT_FALSE(tm->remove_file(0, std::string("nobody"), &path));
  EXPECT_EQ(path, "");
  tm->commit(true);
}

// a checkpoint written by LakeVilla replaces the log before it
TEST_P(TransactionTest, CreateCheckpoint) {
  {
    auto tm = manager(1);
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    ASSERT_TRUE(insert(*tm, "user9"));
    ASSERT_TRUE(tm->remove_file(0, std::string("user0")));
    ASSERT_TRUE(tm->commit(false));
  }
  {
    auto tm = manager(2);
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    EXPECT_TRUE(tm->create_checkpoint(0));
    tm->commit(true);
  }

  auto last = conn.get(base + "_delta_log/_last_checkpoint");
  ASSERT_TRUE(last.has_value());
  auto doc = LHHelpers::readJSON_rapid(last->c_str());
  auto version = (*doc)["version"].GetUint64();
  EXPECT_GE(version, 1u);
  ASSERT_TRUE(conn.check(base + "_delta_log/" +
                         LHTransactions::log_file_name(
                             version, ".checkpoint.parquet")));

  // log cleanup: only the checkpoint describes the table now
  for (uint64_t v = 0; v <= version; v++) {
    conn.delete_file(version_path(base, v));
  }

  auto expected = std::vector<std::string>{"user1", "user2", "user3", "user4",
                                           "user9"};
  EXPECT_EQ(current_keys(), expected);

  auto tm = manager(3);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  std::vector<std::pair<std::string, std::string>> result;
  std::string path;
  EXPECT_NE(tm->read_file(0, "user9", nullptr, result, path), "");
  ASSERT_TRUE(update(*tm, "user3", "after-checkpoint"));
  EXPECT_TRUE(tm->commit(false));
}

TEST_P(TransactionTest, CheckpointRefusesUncommittedChanges) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  EXPECT_FALSE(tm->create_checkpoint(0));
  tm->commit(true);
}

TEST_P(TransactionTest, CheckpointRefusesUnsupportedFeatures) {
  // deletion vectors would need the deletionVector column
  lvtest::write_version(
      conn, base, 1,
      {R"({"add":{"path":"part-dv.snappy.parquet","partitionValues":{},"size":1,"modificationTime":1,"dataChange":true,"deletionVector":{"storageType":"u","pathOrInlineDv":"ab","offset":1,"sizeInBytes":36,"cardinality":1}}})"});
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  EXPECT_FALSE(tm->create_checkpoint(0));
  tm->commit(true);
}

namespace {
std::optional<LHHelpers::CheckpointContent> read_checkpoint(
    MemoryConnector& conn, const std::string& base, uint64_t version) {
  auto data = conn.get(base + "_delta_log/" +
                       LHTransactions::log_file_name(
                           version, ".checkpoint.parquet"));
  if (!data) {
    return std::nullopt;
  }
  return LHHelpers::readDLCheckpointContent(data->data(), data->size());
}

// versions of the classic checkpoints of a table
std::vector<uint64_t> checkpoint_versions(MemoryConnector& conn,
                                          const std::string& base) {
  std::vector<uint64_t> versions;
  auto objects = conn.list_objects(base + "_delta_log/");
  for (auto& object : *objects) {
    if (object.key.find(".checkpoint.parquet") != std::string::npos) {
      versions.push_back(
          std::stoull(object.key.substr(object.key.rfind('/') + 1, 20)));
    }
  }
  std::sort(versions.begin(), versions.end());
  return versions;
}

std::optional<uint64_t> last_checkpoint(MemoryConnector& conn,
                                        const std::string& base) {
  auto last = conn.get(base + "_delta_log/_last_checkpoint");
  if (!last) {
    return std::nullopt;
  }
  auto doc = LHHelpers::readJSON_rapid(last->c_str());
  return (*doc)["version"].GetUint64();
}

LHTransactions::ProtocolOptions with_checkpoints(bool enabled,
                                                 uint32_t interval) {
  LHTransactions::ProtocolOptions options;
  options.checkpoints = enabled;
  options.checkpoint_interval = interval;
  return options;
}
}  // namespace

// streaming writers (txn) and table features (domainMetadata) are kept
TEST_P(TransactionTest, CheckpointKeepsTxnAndDomainMetadata) {
  lvtest::write_version(
      conn, base, 1,
      {R"({"txn":{"appId":"stream","version":3,"lastUpdated":1}})",
       R"({"domainMetadata":{"domain":"kept","configuration":"{\"a\":1}","removed":false}})",
       R"({"domainMetadata":{"domain":"dropped","configuration":"{}","removed":false}})"});
  lvtest::write_version(
      conn, base, 2,
      {R"({"txn":{"appId":"stream","version":4,"lastUpdated":2}})",
       R"({"domainMetadata":{"domain":"dropped","configuration":"{}","removed":true}})"});
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(tm->create_checkpoint(0));
  tm->commit(true);

  auto content = read_checkpoint(conn, base, *last_checkpoint(conn, base));
  ASSERT_TRUE(content.has_value());
  EXPECT_TRUE(content->unsupported.empty());
  ASSERT_EQ(content->txns.size(), 1u);
  EXPECT_EQ(content->txns[0].app_id, "stream");
  EXPECT_EQ(content->txns[0].version, 4);
  EXPECT_EQ(content->txns[0].last_updated, 2);
  ASSERT_EQ(content->domains.size(), 1u);
  EXPECT_EQ(content->domains[0].domain, "kept");
  EXPECT_EQ(content->domains[0].configuration, R"({"a":1})");
}

// partitionValues, tags and modificationTime of the add actions and the
// extended file metadata of tombstones are written back unchanged
TEST_P(TransactionTest, CheckpointPreservesFileMetadata) {
  auto content = lvtest::ycsb_parquet("user7");
  conn.put(base + "region=eu/part-1.snappy.parquet", content);
  lvtest::write_version(
      conn, base, 1,
      {R"({"add":{"path":"region=eu/part-1.snappy.parquet","partitionValues":{"region":"eu","day":null},"size":)" +
           std::to_string(content.size()) +
           R"(,"modificationTime":1234,"dataChange":true,"tags":{"INSERTION_TIME":"42"}}})",
       R"({"remove":{"path":"part-00001-test.snappy.parquet","deletionTimestamp":)" +
           std::to_string(LHHelpers::now_ms()) +
           R"(,"dataChange":true,"extendedFileMetadata":true,"partitionValues":{},"size":77}})"});
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(tm->create_checkpoint(0));
  tm->commit(true);

  auto checkpoint = read_checkpoint(conn, base, *last_checkpoint(conn, base));
  ASSERT_TRUE(checkpoint.has_value());
  auto add = std::find_if(
      checkpoint->adds.begin(), checkpoint->adds.end(),
      [](auto& a) { return a.path == "region=eu/part-1.snappy.parquet"; });
  ASSERT_NE(add, checkpoint->adds.end());
  EXPECT_EQ(add->partition_values, R"({"region":"eu","day":null})");
  EXPECT_EQ(add->tags, R"({"INSERTION_TIME":"42"})");
  EXPECT_EQ(add->modification_time, 1234);
  EXPECT_EQ(add->size, static_cast<int64_t>(content.size()));
  ASSERT_EQ(checkpoint->removes.size(), 1u);
  EXPECT_EQ(checkpoint->removes[0].path, "part-00001-test.snappy.parquet");
  EXPECT_EQ(checkpoint->removes[0].size, 77);
  EXPECT_EQ(checkpoint->removes[0].partition_values, "{}");
}

// a file removed and added again (e.g. RESTORE) is part of the snapshot and
// of the checkpoint, whatever the order in which the versions are parsed
TEST_P(TransactionTest, ReAddedFileSurvivesCheckpoint) {
  lvtest::write_version(conn, base, 1,
                        {lvtest::remove_action("part-00000-test.snappy.parquet")});
  auto size = conn.get(base + "part-00000-test.snappy.parquet")->size();
  lvtest::write_version(
      conn, base, 2,
      {lvtest::add_action("part-00000-test.snappy.parquet", size, "user0")});
  auto sorted_keys = [this] {
    auto keys = current_keys();
    std::sort(keys.begin(), keys.end());
    return keys;
  };
  EXPECT_EQ(sorted_keys(), KEYS);

  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(tm->create_checkpoint(0));
  tm->commit(true);
  auto version = *last_checkpoint(conn, base);
  for (uint64_t v = 0; v <= version; v++) {
    conn.delete_file(version_path(base, v));
  }
  EXPECT_EQ(sorted_keys(), KEYS);
}

// a commit of a multiple of the checkpoint interval writes a checkpoint
// (like Spark); readers start from it after a log cleanup
TEST_P(TransactionTest, CommitWritesCheckpointAtInterval) {
  std::vector<std::string> expected = KEYS;
  for (int i = 0; i < 6; i++) {
    auto key = "user" + std::to_string(10 + i);
    auto tm = manager(1 + i);
    tm->set_protocol_options(with_checkpoints(true, 3));
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    ASSERT_TRUE(insert(*tm, key));
    ASSERT_TRUE(tm->commit(false));
    expected.push_back(key);
  }

  auto versions = checkpoint_versions(conn, base);
  ASSERT_FALSE(versions.empty());
  for (auto v : versions) {
    EXPECT_EQ(v % 3, 0u) << v;
    // a checkpoint is written only for a final version
    auto actions = actions_of(conn, base, v);
    EXPECT_FALSE(actions.empty());
  }
  EXPECT_EQ(last_checkpoint(conn, base), versions.back());

  for (uint64_t v = 0; v < versions.back(); v++) {
    conn.delete_file(version_path(base, v));
  }
  auto keys = current_keys();
  std::sort(keys.begin(), keys.end());
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(keys, expected);
}

TEST_P(TransactionTest, CheckpointsCanBeDisabled) {
  for (int i = 0; i < 4; i++) {
    auto tm = manager(1 + i);
    tm->set_protocol_options(with_checkpoints(false, 2));
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    ASSERT_TRUE(insert(*tm, "user" + std::to_string(10 + i)));
    ASSERT_TRUE(tm->commit(false));
  }
  EXPECT_TRUE(checkpoint_versions(conn, base).empty());
  EXPECT_FALSE(last_checkpoint(conn, base).has_value());
}

// the table property delta.checkpointInterval applies without an override
TEST_P(TransactionTest, CheckpointIntervalFromTableProperty) {
  auto protocol_and_metadata = lvtest::protocol_and_metadata();
  auto pos = protocol_and_metadata.find(R"("configuration":{})");
  ASSERT_NE(pos, std::string::npos);
  protocol_and_metadata.replace(
      pos, 18, R"("configuration":{"delta.checkpointInterval":"2"})");
  lvtest::write_version(conn, base, 1,
                        lvtest::split_lines(protocol_and_metadata));
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user10"));
  ASSERT_TRUE(tm->commit(false));
  EXPECT_EQ(checkpoint_versions(conn, base), std::vector<uint64_t>{2});
}

// without _last_checkpoint, the listing of the log shows the checkpoint
TEST_P(TransactionTest, ListingFindsCheckpointWithoutLastCheckpoint) {
  if (parse_levels(GetParam())[2]) {
    GTEST_SKIP() << "level 2 does not list the log of a table in the global "
                    "log";
  }
  {
    auto tm = manager(1);
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    ASSERT_TRUE(insert(*tm, "user9"));
    ASSERT_TRUE(tm->commit(false));
  }
  {
    auto tm = manager(2);
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    ASSERT_TRUE(tm->create_checkpoint(0));
    tm->commit(true);
  }
  auto version = *last_checkpoint(conn, base);
  conn.delete_file(base + "_delta_log/_last_checkpoint");
  for (uint64_t v = 0; v < version; v++) {
    conn.delete_file(version_path(base, v));
  }
  auto keys = current_keys();
  std::sort(keys.begin(), keys.end());
  EXPECT_EQ(keys, (std::vector<std::string>{"user0", "user1", "user2",
                                            "user3", "user4", "user9"}));
}

// level 2: a snapshot of the global log older than _last_checkpoint starts
// from an older checkpoint (the versions before it were cleaned up)
TEST_P(TransactionTest, GlobalLogSnapshotUsesOlderCheckpoint) {
  if (!parse_levels(GetParam())[2]) {
    GTEST_SKIP() << "level 2";
  }
  std::vector<std::string> inserted;
  for (int i = 0; i < 4; i++) {
    auto key = "user" + std::to_string(10 + i);
    auto tm = manager(1 + i);
    tm->set_protocol_options(with_checkpoints(true, 2));
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    ASSERT_TRUE(insert(*tm, key));
    ASSERT_TRUE(tm->commit(false));
    inserted.push_back(key);
  }
  ASSERT_EQ(checkpoint_versions(conn, base), (std::vector<uint64_t>{2, 4}));
  ASSERT_EQ(last_checkpoint(conn, base), 4u);
  conn.delete_file(version_path(base, 0));
  conn.delete_file(version_path(base, 1));
  conn.put(LV_DEFAULT_GLOBAL_LOG, base + " 3\n");

  auto tm = manager(10);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  auto keys = lvtest::keys_of(tm->read_table(0));
  tm->commit(true);
  std::sort(keys.begin(), keys.end());
  std::vector<std::string> expected = KEYS;
  expected.insert(expected.end(), inserted.begin(), inserted.begin() + 3);
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(keys, expected);
}

// concurrent writers on separate threads: every successful commit is
// visible, no failed commit leaks data
TEST_P(TransactionTest, ParallelWriters) {
  const int writers = 8;
  std::vector<int> committed(writers, 0);
  {
    std::vector<std::jthread> threads;
    for (int i = 0; i < writers; i++) {
      threads.emplace_back([this, i, &committed] {
        auto tm = manager(100 + i);
        tm->set_global_lock_timing(10000, 30000);
        if (!tm->begin_transaction_ycsb()) {
          return;
        }
        if (!insert(*tm, "writer" + std::to_string(i))) {
          tm->commit(true);
          return;
        }
        committed[i] = tm->commit(false) ? 1 : 0;
      });
    }
  }

  auto expected = KEYS;
  for (int i = 0; i < writers; i++) {
    if (committed[i]) {
      expected.push_back("writer" + std::to_string(i));
    }
  }
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(current_keys(), expected);
  if (uses_markers()) {
    EXPECT_EQ(std::count(committed.begin(), committed.end(), 1), writers);
  } else {
    EXPECT_GE(std::count(committed.begin(), committed.end(), 1), 1);
  }
}

// B04: the YCSB/Hermitage drivers remove the old file on a helper thread
// while adding the new one: operations of one transaction run concurrently
TEST_P(TransactionTest, B04_ConcurrentOperationsInOneTransaction) {
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  for (auto& key : KEYS) {
    std::string path;
    auto old = tm->read_file_as_table(0, key, path);
    ASSERT_NE(old, nullptr);
    std::vector<std::pair<std::string, std::string>> values = {
        {"field0", "new-" + key}};
    auto prev = tm->register_update_pairs(
        values, LHTransactions::UpdateOperations::REP, values);
    std::jthread remover([&] { tm->remove_file(0, key, &path, prev); });
    ASSERT_TRUE(tm->add_file(0, lvtest::ycsb_table(key, "new-" + key), key,
                             prev));
  }
  ASSERT_TRUE(tm->commit(false));
  tm = nullptr;

  auto table = current_table();
  EXPECT_EQ(lvtest::keys_of(table), KEYS);
  for (auto& key : KEYS) {
    EXPECT_EQ(field0_of(table, key), "new-" + key);
  }
}

// files of other engines may spell column names differently (Delta
// column names are case-insensitive), e.g. Spark writes FIELD0 for field0
TEST_P(TransactionTest, ReadTableWithDifferentColumnCase) {
  auto upper = lvtest::ycsb_table("user7");
  std::vector<std::string> names;
  for (auto& name : upper->ColumnNames()) {
    std::string up = name;
    std::transform(up.begin(), up.end(), up.begin(), ::toupper);
    names.push_back(up);
  }
  upper = *upper->RenameColumns(names);
  auto content = LHHelpers::encodeAsParquet_str(upper);
  conn.put(base + "part-spark.parquet", content);
  lvtest::write_version(
      conn, base, 1,
      {lvtest::add_action("part-spark.parquet", content.size(), "user7")});

  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  auto table = tm->read_table(0);
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->num_rows(), 6);
  EXPECT_EQ(table->ColumnNames()[1], "field0");
  tm->commit(true);
}

TEST_P(TransactionTest, DefaultGlobalLogPaths) {
  LHTransactions::set_default_global_log_paths("cfg/log.txt", "cfg/log.tmp");
  auto tm = manager(1);
  LHTransactions::set_default_global_log_paths(LV_DEFAULT_GLOBAL_LOG,
                                               LV_DEFAULT_GLOBAL_TMP_LOG);
  EXPECT_EQ(tm->global_log_path, "cfg/log.txt");
  EXPECT_EQ(tm->global_tmp_log_path, "cfg/log.tmp");
}

TEST(TransactionIds, Unique) {
  std::set<uint32_t> ids;
  for (int i = 0; i < 1000; i++) {
    ids.insert(LHTransactions::unique_transaction_id());
  }
  EXPECT_EQ(ids.size(), 1000u);
  EXPECT_EQ(ids.count(UINT32_MAX), 0u);
}

// concurrent read-modify-write transactions on the same key must never
// leave two versions of the row behind
TEST_P(TransactionTest, ConcurrentUpdatesOfOneKey) {
  const int writers = 2;
  const int updates = 10;
  std::atomic<int> committed = 0;
  {
    std::vector<std::jthread> threads;
    for (int w = 0; w < writers; w++) {
      threads.emplace_back([this, w, &committed] {
        auto tm = manager(200 + w);
        tm->set_global_lock_timing(10000, 30000);
        for (int i = 0; i < updates; i++) {
          if (!tm->begin_transaction_ycsb()) {
            continue;
          }
          auto value = "w" + std::to_string(w) + "-" + std::to_string(i);
          if (!update(*tm, "user1", value)) {
            tm->commit(true);
            continue;
          }
          if (tm->commit(false)) {
            committed++;
          }
        }
      });
    }
  }
  EXPECT_GE(committed.load(), 1);
  auto keys = current_keys();
  EXPECT_EQ(keys, KEYS);
}

TEST_P(TransactionTest, InterleavedUpdatesOfOneKey) {
  auto tm1 = manager(1);
  auto tm2 = manager(2);
  ASSERT_TRUE(tm1->begin_transaction_ycsb());
  ASSERT_TRUE(tm2->begin_transaction_ycsb());
  ASSERT_TRUE(update(*tm1, "user1", "t1"));
  ASSERT_TRUE(tm1->commit(false));
  ASSERT_TRUE(update(*tm2, "user1", "t2"));
  bool second = tm2->commit(false);
  tm1 = nullptr;
  tm2 = nullptr;
  EXPECT_EQ(current_keys(), KEYS);
  // the last committed update wins
  EXPECT_EQ(field0_of(current_table(), "user1"), second ? "t2" : "t1");
}

// Hermitage G2: two transactions insert raw Parquet files concurrently
TEST_P(TransactionTest, InterleavedRawInserts) {
  auto tm1 = manager(1);
  auto tm2 = manager(2);
  ASSERT_TRUE(tm1->begin_transaction_ycsb());
  ASSERT_TRUE(tm2->begin_transaction_ycsb());
  tm1->read_table(0);
  tm2->read_table(0);
  auto content1 = lvtest::ycsb_parquet("user8");
  auto content2 = lvtest::ycsb_parquet("user9");
  ASSERT_TRUE(tm1->add_file(0, content1, "user8"));
  ASSERT_TRUE(tm2->add_file(0, content2, "user9"));
  EXPECT_TRUE(tm1->commit(false));
  bool second = tm2->commit(false);
  tm1 = nullptr;
  tm2 = nullptr;
  auto expected = KEYS;
  expected.push_back("user8");
  if (second) {
    expected.push_back("user9");
  }
  EXPECT_EQ(current_keys(), expected);
}

// ---------------------------------------------------------------------------
// marker leases
// ---------------------------------------------------------------------------
namespace {
LHTransactions::ProtocolOptions short_lease() {
  LHTransactions::ProtocolOptions options;
  options.marker_lease_ms = 300;
  options.marker_grace_ms = 100;
  return options;
}

// a marker left behind by a crashed client
std::string stale_marker(uint32_t txn, std::optional<int64_t> deadline) {
  std::string marker = R"({"commitInfo":{"txnId":)" + std::to_string(txn) +
                       R"(,"operation":"WRITE"}})" "\n" +
                       R"({"marker":{"workDir":"wh/crashed/","running":true,"dependencies":[],"txnid":)" +
                       std::to_string(txn);
  if (deadline) {
    marker += R"(,"deadline":)" + std::to_string(*deadline);
  }
  return marker + "}}";
}
}  // namespace

TEST_P(TransactionTest, LeaseMarkerHasDeadline) {
  if (!uses_markers()) GTEST_SKIP() << "markers only";
  auto tm = manager(1);
  auto before = StorageConnector::local_time_ms();
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  auto marker = *conn.get(version_path(base, 1));
  auto line = marker.substr(marker.find('\n') + 1);
  auto doc = LHHelpers::readJSON_rapid(line.c_str());
  ASSERT_TRUE((*doc)["marker"].HasMember("deadline"));
  auto deadline = (*doc)["marker"]["deadline"].GetInt64();
  EXPECT_GE(deadline, before + 30000);
  EXPECT_LE(deadline, StorageConnector::local_time_ms() + 30000);
  tm->commit(true);
}

TEST_P(TransactionTest, LeaseCrashedHolderIsTakenOver) {
  if (!uses_markers()) GTEST_SKIP() << "markers only";
  conn.put(version_path(base, 1),
           stale_marker(77, StorageConnector::local_time_ms() + 200));

  auto tm = manager(1);
  tm->set_protocol_options(short_lease());
  auto start = StorageConnector::local_time_ms();
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  EXPECT_TRUE(tm->commit(false));
  auto waited = StorageConnector::local_time_ms() - start;
  tm = nullptr;

  // waited for the lease and the grace period, then took the version over
  EXPECT_GE(waited, 250);
  EXPECT_LT(waited, 5000);
  EXPECT_NE(conn.get(version_path(base, 1))->find("MarkerTakeover"),
            std::string::npos);
  EXPECT_EQ(conn.get(version_path(base, 1))->find("\"marker\""),
            std::string::npos);
  auto expected = KEYS;
  expected.push_back("user9");
  EXPECT_EQ(current_keys(), expected);
}

TEST_P(TransactionTest, LeaseMarkerWithoutDeadline) {
  if (!uses_markers()) GTEST_SKIP() << "markers only";
  // older clients write no deadline: the lease runs from Last-Modified
  conn.put(version_path(base, 1), stale_marker(78, std::nullopt));

  auto tm = manager(1);
  tm->set_protocol_options(short_lease());
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  EXPECT_TRUE(tm->commit(false));
  EXPECT_NE(conn.get(version_path(base, 1))->find("MarkerTakeover"),
            std::string::npos);
}

TEST_P(TransactionTest, LeaseExpiredHolderShifts) {
  if (!uses_markers()) GTEST_SKIP() << "markers only";
  auto tm = manager(1);
  tm->set_protocol_options(short_lease());
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  EXPECT_TRUE(tm->commit(false));
  tm = nullptr;

  // version 1 was released empty, the commit moved to version 2
  EXPECT_TRUE(actions_of(conn, base, 1).empty());
  EXPECT_EQ(conn.get(version_path(base, 1))->find("\"marker\""),
            std::string::npos);
  auto actions = actions_of(conn, base, 2);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_NE(actions[0].find("\"add\""), std::string::npos);
  auto expected = KEYS;
  expected.push_back("user9");
  EXPECT_EQ(current_keys(), expected);
}

TEST_P(TransactionTest, LeaseWaiterTakesOverAndHolderMovesOn) {
  if (!uses_markers()) GTEST_SKIP() << "markers only";
  auto tm1 = manager(1);
  auto tm2 = manager(2);
  tm1->set_protocol_options(short_lease());
  tm2->set_protocol_options(short_lease());
  ASSERT_TRUE(tm1->begin_transaction_ycsb());  // version 1
  ASSERT_TRUE(tm2->begin_transaction_ycsb());  // version 2
  ASSERT_TRUE(insert(*tm1, "user8"));
  ASSERT_TRUE(insert(*tm2, "user9"));

  // tm2 waits for tm1's lease, takes version 1 over and commits version 2
  EXPECT_TRUE(tm2->commit(false));
  EXPECT_NE(conn.get(version_path(base, 1))->find("MarkerTakeover"),
            std::string::npos);
  // tm1's lease is gone: it moves to a later version
  EXPECT_TRUE(tm1->commit(false));
  tm1 = nullptr;
  tm2 = nullptr;

  auto expected = KEYS;
  expected.push_back("user8");
  expected.push_back("user9");
  EXPECT_EQ(current_keys(), expected);
}

TEST_P(TransactionTest, LeaseTakenOverDespiteHolderClock) {
  if (!uses_markers()) GTEST_SKIP() << "markers only";
  // the waiter's clock runs ahead: it takes over while the holder still
  // believes its lease is valid; the holder's commit is fenced (ETag)
  int64_t skew = 0;
  conn.clock = [&skew] { return StorageConnector::local_time_ms() + skew; };

  auto tm1 = manager(1);
  auto tm2 = manager(2);
  ASSERT_TRUE(tm1->begin_transaction_ycsb());  // 30 s lease, version 1
  ASSERT_TRUE(tm2->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm1, "user8"));
  ASSERT_TRUE(insert(*tm2, "user9"));

  skew = 60000;
  EXPECT_TRUE(tm2->commit(false));  // takes version 1 over
  skew = 0;
  EXPECT_TRUE(tm1->commit(false));  // 412 on version 1 -> later version
  conn.clock = nullptr;
  tm1 = nullptr;
  tm2 = nullptr;

  EXPECT_NE(conn.get(version_path(base, 1))->find("MarkerTakeover"),
            std::string::npos);
  auto expected = KEYS;
  expected.push_back("user8");
  expected.push_back("user9");
  EXPECT_EQ(current_keys(), expected);
}

TEST_P(TransactionTest, LeaseRepeatedExpiryAborts) {
  if (!uses_markers()) GTEST_SKIP() << "markers only";
  auto options = short_lease();
  options.marker_lease_ms = 0;  // expires before every commit
  auto tm = manager(1);
  tm->set_protocol_options(options);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  EXPECT_FALSE(tm->commit(false));
  tm = nullptr;

  EXPECT_EQ(current_keys(), KEYS);
  // every reserved version was released
  for (auto& key : conn.keys(base + "_delta_log/")) {
    if (key.find(".json") != std::string::npos) {
      EXPECT_EQ(conn.get(key)->find("\"marker\""), std::string::npos) << key;
    }
  }
}

TEST_P(TransactionTest, LeaseTwoWaitersRace) {
  if (!uses_markers()) GTEST_SKIP() << "markers only";
  conn.put(version_path(base, 1),
           stale_marker(79, StorageConnector::local_time_ms() + 200));
  std::vector<int> committed(2, 0);
  {
    std::vector<std::jthread> threads;
    for (int i = 0; i < 2; i++) {
      threads.emplace_back([this, i, &committed] {
        auto tm = manager(300 + i);
        tm->set_protocol_options(short_lease());
        if (tm->begin_transaction_ycsb() &&
            insert(*tm, "waiter" + std::to_string(i))) {
          committed[i] = tm->commit(false) ? 1 : 0;
        }
      });
    }
  }
  EXPECT_EQ(committed[0] + committed[1], 2);
  EXPECT_NE(conn.get(version_path(base, 1))->find("MarkerTakeover"),
            std::string::npos);
  auto keys = current_keys();
  EXPECT_EQ(std::count(keys.begin(), keys.end(), "waiter0"), 1);
  EXPECT_EQ(std::count(keys.begin(), keys.end(), "waiter1"), 1);
}

// ---------------------------------------------------------------------------
// isolation
// ---------------------------------------------------------------------------
namespace {
LHTransactions::ProtocolOptions with_isolation(LHTransactions::Isolation i) {
  LHTransactions::ProtocolOptions options;
  options.isolation = i;
  return options;
}

// field0 of key += delta; implicitly logged transactions record the delta
// (ADD) and recompute it on redo
bool add_to_field0(TransactionManagerDeltaLake& tm, const std::string& key,
                   double delta) {
  std::string path;
  auto old = tm.read_file_as_table(0, key, path);
  if (!old) {
    return false;
  }
  auto column = std::static_pointer_cast<arrow::StringArray>(
      old->GetColumnByName("field0")->chunk(0));
  double value = std::stod(column->GetString(0)) + delta;
  std::vector<std::pair<std::string, std::string>> values = {
      {"field0", std::to_string(value)}};
  std::vector<std::pair<std::string, std::string>> impl = {
      {"field0", std::to_string(delta)}};
  auto prev = tm.register_update_pairs(
      values, LHTransactions::UpdateOperations::ADD, impl);
  tm.remove_file(0, key, &path, prev);
  return tm.add_file(0, lvtest::ycsb_table(key, std::to_string(value)), key,
                     prev);
}
}  // namespace

TEST_P(TransactionTest, SerializableWriteSkew) {
  auto t1 = manager(1);
  auto t2 = manager(2);
  ASSERT_TRUE(t1->begin_transaction_ycsb());
  ASSERT_TRUE(t2->begin_transaction_ycsb());
  std::vector<std::pair<std::string, std::string>> r;
  std::string p;
  t1->read_file(0, "user1", nullptr, r, p);
  t2->read_file(0, "user2", nullptr, r, p);
  ASSERT_TRUE(update(*t1, "user2", "t1"));
  ASSERT_TRUE(update(*t2, "user1", "t2"));
  EXPECT_TRUE(t1->commit(false));
  bool second = t2->commit(false);
  t1 = nullptr;
  t2 = nullptr;

  auto table = current_table();
  EXPECT_EQ(lvtest::keys_of(table), KEYS);
  EXPECT_EQ(field0_of(table, "user2"), "t1");
  if (parse_levels(GetParam())[0]) {
    // T2 saw T1's marker: its logged operations are redone after T1
    // (equivalent to the serial order T1, T2)
    EXPECT_TRUE(second);
    EXPECT_EQ(field0_of(table, "user1"), "t2");
  } else {
    // T2 cannot redo: it read a table T1 changed and aborts
    EXPECT_FALSE(second);
    EXPECT_EQ(field0_of(table, "user1"), "v");
  }
}

TEST_P(TransactionTest, SnapshotIsolationAllowsWriteSkew) {
  auto levels = parse_levels(GetParam());
  if (!(levels[1] && !levels[0])) {
    GTEST_SKIP() << "only configurations without redo and with markers";
  }
  auto t1 = manager(1);
  auto t2 = manager(2);
  t1->set_protocol_options(with_isolation(LHTransactions::Isolation::Snapshot));
  t2->set_protocol_options(with_isolation(LHTransactions::Isolation::Snapshot));
  ASSERT_TRUE(t1->begin_transaction_ycsb());
  ASSERT_TRUE(t2->begin_transaction_ycsb());
  ASSERT_TRUE(update(*t1, "user2", "t1"));
  ASSERT_TRUE(update(*t2, "user1", "t2"));
  EXPECT_TRUE(t1->commit(false));
  EXPECT_TRUE(t2->commit(false));
}

TEST_P(TransactionTest, SerializableEmptyVersionsDoNotConflict) {
  auto t1 = manager(1);
  auto t2 = manager(2);
  ASSERT_TRUE(t1->begin_transaction_ycsb());
  ASSERT_TRUE(t2->begin_transaction_ycsb());
  t1->read_table(0);
  EXPECT_TRUE(t1->commit(true));  // read-only: an empty version (markers)
  ASSERT_TRUE(update(*t2, "user1", "t2"));
  EXPECT_TRUE(t2->commit(false));
}

TEST_P(TransactionTest, SerializableCrossTableWriteSkew) {
  for (auto isolation : {LHTransactions::Isolation::Serializable,
                         LHTransactions::Isolation::Snapshot}) {
    conn.clear();
    lvtest::create_ycsb_table(conn, base, KEYS);
    std::string other = "wh/other.db/accounts/";
    lvtest::create_ycsb_table(conn, other, {"acc0"});

    auto t1 = manager(1);
    auto t2 = manager(2);
    t1->set_protocol_options(with_isolation(isolation));
    t2->set_protocol_options(with_isolation(isolation));
    ASSERT_TRUE(t1->begin_transaction_ycsb());
    ASSERT_TRUE(t2->begin_transaction_ycsb());
    ASSERT_TRUE(t1->open_new_table(other));
    ASSERT_TRUE(t2->open_new_table(other));
    // T1 reads the user table and writes the other table, T2 vice versa
    t1->read_table(0);
    t2->read_table(1);
    ASSERT_TRUE(t1->add_file(1, lvtest::ycsb_table("acc1"), "acc1"));
    ASSERT_TRUE(t2->add_file(0, lvtest::ycsb_table("user9"), "user9"));
    EXPECT_TRUE(t1->commit(false));
    bool second = t2->commit(false);
    t1 = nullptr;
    t2 = nullptr;

    bool redo = parse_levels(GetParam())[0];
    if (isolation == LHTransactions::Isolation::Serializable && !redo) {
      EXPECT_FALSE(second) << "serializable";
    } else {
      EXPECT_TRUE(second);
    }
  }
}

TEST_P(TransactionTest, SerializableRedoRounds) {
  auto levels = parse_levels(GetParam());
  if (!levels[0] || levels[2]) {
    // with level 2 the snapshot comes from the global log: markers after it
    // are not seen at begin, transactions log explicitly and cannot redo
    GTEST_SKIP() << "redo needs level 0 and a snapshot that sees the markers";
  }
  for (uint32_t max_redos : {3u, 1u}) {
    conn.clear();
    lvtest::create_ycsb_table(conn, base, KEYS);
    {
      auto t0 = manager(10);
      ASSERT_TRUE(t0->begin_transaction_ycsb());
      ASSERT_TRUE(update(*t0, "user1", "100"));
      ASSERT_TRUE(t0->commit(false));
    }

    auto options = short_lease();
    options.max_redos = max_redos;
    auto t1 = manager(1);
    auto t2 = manager(2);
    auto t3 = manager(3);
    t2->set_protocol_options(options);
    ASSERT_TRUE(t1->begin_transaction_ycsb());
    ASSERT_TRUE(t2->begin_transaction_ycsb());  // implicit: sees T1
    ASSERT_TRUE(t3->begin_transaction_ycsb());  // implicit: sees T1, T2

    ASSERT_TRUE(add_to_field0(*t1, "user1", 1));
    EXPECT_TRUE(t1->commit(false));  // 101

    // T2's lease ends; T3 takes T2's version over and commits first
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(add_to_field0(*t3, "user1", 10));
    EXPECT_TRUE(t3->commit(false));  // 111

    // T2 redoes after T1, moves behind T3 (shift) and redoes again
    ASSERT_TRUE(add_to_field0(*t2, "user1", 100));
    bool committed = t2->commit(false);
    t1 = nullptr;
    t2 = nullptr;
    t3 = nullptr;

    auto table = current_table();
    EXPECT_EQ(lvtest::keys_of(table), KEYS);
    double value = std::stod(field0_of(table, "user1"));
    if (max_redos >= 3) {
      EXPECT_TRUE(committed);
      EXPECT_DOUBLE_EQ(value, 211);  // every update applied exactly once
    } else {
      EXPECT_FALSE(committed) << "aborts after max_redos rounds";
      EXPECT_DOUBLE_EQ(value, 111);
    }
  }
}

// a commit releases the versions reserved for tables it did not change
TEST_P(TransactionTest, CommitReleasesUnchangedTables) {
  if (!uses_markers()) GTEST_SKIP() << "markers only";
  std::string other = "wh/other.db/accounts/";
  lvtest::create_ycsb_table(conn, other, {"acc0"});
  auto tm = manager(1);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(tm->open_new_table(other));
  tm->read_table(1);
  ASSERT_TRUE(insert(*tm, "user9"));
  ASSERT_TRUE(tm->commit(false));
  // tm still exists: its version of the other table must not be reserved
  auto content = conn.get(other + "_delta_log/" + lvtest::log_name(1));
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(content->find("\"marker\""), std::string::npos);
}

namespace {
LHTransactions::ProtocolOptions with_repair(bool repair) {
  LHTransactions::ProtocolOptions options;
  options.global_log_repair = repair;
  return options;
}

// a commit of another engine (data file + add action) at version
void foreign_commit(MemoryConnector& conn, const std::string& base,
                    uint64_t version, const std::string& key,
                    const std::string& engine = "Apache-Spark/3.5.3") {
  auto content = lvtest::ycsb_parquet(key);
  std::string file = "part-" + key + ".parquet";
  conn.put(base + file, content);
  lvtest::write_version(
      conn, base, version,
      {R"({"commitInfo":{"engineInfo":")" + engine + R"("}})",
       lvtest::add_action(file, content.size(), key)});
}

bool contains(const std::vector<std::string>& keys, const std::string& key) {
  return std::count(keys.begin(), keys.end(), key) > 0;
}
}  // namespace

// snapshot isolation: a reader with an older snapshot must not move the
// global log back
TEST_P(TransactionTest, SnapshotReaderKeepsGlobalLogVersion) {
  if (!parse_levels(GetParam())[2] || uses_markers()) {
    GTEST_SKIP() << "level 2 without markers";
  }
  auto reader = manager(1);
  reader->set_protocol_options(
      with_isolation(LHTransactions::Isolation::Snapshot));
  ASSERT_TRUE(reader->begin_transaction_ycsb());
  reader->read_table(0);
  {
    auto writer = manager(2);
    ASSERT_TRUE(writer->begin_transaction_ycsb());
    ASSERT_TRUE(insert(*writer, "user9"));
    ASSERT_TRUE(writer->commit(false));
  }
  EXPECT_TRUE(reader->commit(false));
  EXPECT_EQ(*conn.get(LV_DEFAULT_GLOBAL_LOG), base + " 1\n");
}

// LV[Read]: a foreign commit behind the global log blocks every later
// commit; GLOBAL_LOG_REPAIR advances the global log
TEST_P(TransactionTest, StaleGlobalLogIsRepaired) {
  if (!parse_levels(GetParam())[2] || uses_markers()) {
    GTEST_SKIP() << "LV[Read] (level 2 without markers)";
  }
  for (bool repair : {false, true}) {
    conn.clear();
    lvtest::create_ycsb_table(conn, base, KEYS);
    {
      auto tm = manager(1);
      ASSERT_TRUE(tm->begin_transaction_ycsb());
      ASSERT_TRUE(insert(*tm, "user8"));
      ASSERT_TRUE(tm->commit(false));
    }
    foreign_commit(conn, base, 2, "spark1");

    int committed = 0;
    for (int i = 0; i < 3; i++) {
      auto tm = manager(10 + i);
      tm->set_protocol_options(with_repair(repair));
      ASSERT_TRUE(tm->begin_transaction_ycsb());
      ASSERT_TRUE(insert(*tm, "user" + std::to_string(20 + i)));
      committed += tm->commit(false);
    }
    auto keys = current_keys();
    if (repair) {
      EXPECT_EQ(committed, 3);
      EXPECT_TRUE(contains(keys, "spark1"));
      EXPECT_TRUE(contains(keys, "user22"));
      EXPECT_EQ(*conn.get(LV_DEFAULT_GLOBAL_LOG), base + " 5\n");
    } else {
      EXPECT_EQ(committed, 0);
      EXPECT_FALSE(contains(keys, "spark1"));
      EXPECT_EQ(*conn.get(LV_DEFAULT_GLOBAL_LOG), base + " 1\n");
    }
    EXPECT_FALSE(conn.check(LV_DEFAULT_GLOBAL_TMP_LOG));
  }
}

// a client crashed between its Delta commit and the global-log update
TEST_P(TransactionTest, CrashBeforeGlobalLogUpdateIsRepaired) {
  if (!parse_levels(GetParam())[2] || uses_markers()) {
    GTEST_SKIP() << "LV[Read] (level 2 without markers)";
  }
  {
    auto tm = manager(1);
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    ASSERT_TRUE(insert(*tm, "user8"));
    ASSERT_TRUE(tm->commit(false));
  }
  {
    auto crashed = manager(2);
    crashed->set_storage_retries(1);
    ASSERT_TRUE(crashed->begin_transaction_ycsb());
    ASSERT_TRUE(insert(*crashed, "user9"));
    conn.fail_hook = [](MemoryConnector::Op op, const std::string& path) {
      return op == MemoryConnector::Op::Write && path == LV_DEFAULT_GLOBAL_LOG;
    };
    crashed->commit(false);
    conn.fail_hook = nullptr;
  }
  ASSERT_TRUE(conn.check(version_path(base, 2)));
  ASSERT_EQ(*conn.get(LV_DEFAULT_GLOBAL_LOG), base + " 1\n");

  auto blocked = manager(3);
  ASSERT_TRUE(blocked->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*blocked, "user10"));
  EXPECT_FALSE(blocked->commit(false));

  // serializable: a transaction that read the table aborts after the repair
  // (the repaired version changed what it read), the next one commits
  auto reader = manager(4);
  reader->set_protocol_options(with_repair(true));
  ASSERT_TRUE(reader->begin_transaction_ycsb());
  reader->read_table(0);
  ASSERT_TRUE(insert(*reader, "user11"));
  EXPECT_FALSE(reader->commit(false));
  EXPECT_EQ(*conn.get(LV_DEFAULT_GLOBAL_LOG), base + " 2\n");

  auto next = manager(5);
  next->set_protocol_options(with_repair(true));
  ASSERT_TRUE(next->begin_transaction_ycsb());
  auto keys = lvtest::keys_of(next->read_table(0));
  EXPECT_TRUE(contains(keys, "user9"));
  ASSERT_TRUE(insert(*next, "user12"));
  EXPECT_TRUE(next->commit(false));
  EXPECT_EQ(*conn.get(LV_DEFAULT_GLOBAL_LOG), base + " 3\n");
}

// a running marker in the range postpones the repair
TEST_P(TransactionTest, GlobalLogRepairStopsAtMarker) {
  if (!parse_levels(GetParam())[2] || uses_markers()) {
    GTEST_SKIP() << "LV[Read] (level 2 without markers)";
  }
  {
    auto tm = manager(1);
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    ASSERT_TRUE(insert(*tm, "user8"));
    ASSERT_TRUE(tm->commit(false));
  }
  // a client with markers on the same table reserved version 2
  conn.put(version_path(base, 2), R"({"marker":{"deadline":0}})");
  auto tm = manager(2);
  tm->set_protocol_options(with_repair(true));
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(insert(*tm, "user9"));
  EXPECT_FALSE(tm->commit(false));
  EXPECT_EQ(*conn.get(LV_DEFAULT_GLOBAL_LOG), base + " 1\n");
  EXPECT_FALSE(conn.check(LV_DEFAULT_GLOBAL_TMP_LOG));
}

// GLOBAL_LOG_CHECK_AT_BEGIN: commits of other engines behind the global log
// are visible; LakeVilla commits only through the global log
TEST_P(TransactionTest, CheckAtBeginSeesForeignCommits) {
  if (!parse_levels(GetParam())[2]) {
    GTEST_SKIP() << "global log only with level 2";
  }
  {
    auto tm = manager(1);
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    ASSERT_TRUE(insert(*tm, "user8"));
    ASSERT_TRUE(tm->commit(false));
  }
  foreign_commit(conn, base, 2, "spark1");
  foreign_commit(conn, base, 3, "lv1", "LHTransactions 0.1 - Delta Lake 3.0");

  for (bool check : {false, true}) {
    LHTransactions::ProtocolOptions options;
    options.global_log_check_at_begin = check;
    auto tm = manager(2);
    tm->set_protocol_options(options);
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    auto keys = lvtest::keys_of(tm->read_table(0));
    EXPECT_TRUE(contains(keys, "user8"));
    EXPECT_EQ(contains(keys, "spark1"), check);
    EXPECT_FALSE(contains(keys, "lv1"));
    tm->commit(true);
  }
}

TEST(Checkpoint, NewestCompleteCheckpoint) {
  auto objects = [](std::vector<std::string> names) {
    std::vector<StorageConnector::ObjectInfo> result;
    for (auto& name : names) {
      result.push_back({"t/_delta_log/" + name, 1});
    }
    return result;
  };
  auto newest = [](const std::vector<StorageConnector::ObjectInfo>& list,
                   uint64_t max_version) {
    auto info = LHTransactions::newest_checkpoint(list, max_version);
    return std::make_pair(info.version, info.parts);
  };
  using Result = std::pair<std::optional<uint64_t>, uint32_t>;

  auto list = objects(
      {"00000000000000000009.json", "00000000000000000010.checkpoint.parquet",
       "00000000000000000010.json", "_last_checkpoint",
       // complete multi-part checkpoint
       "00000000000000000020.checkpoint.0000000001.0000000002.parquet",
       "00000000000000000020.checkpoint.0000000002.0000000002.parquet",
       // incomplete (a part is missing)
       "00000000000000000030.checkpoint.0000000001.0000000003.parquet",
       "00000000000000000030.checkpoint.0000000003.0000000003.parquet",
       // V2 checkpoints are not classic checkpoints
       "00000000000000000040.checkpoint.80a083e8-7026-4e79-81be-64bd76c43a11"
       ".parquet"});
  EXPECT_EQ(newest(list, UINT64_MAX), (Result{20, 2}));
  EXPECT_EQ(newest(list, 19), (Result{10, 1}));
  EXPECT_EQ(newest(list, 9), (Result{std::nullopt, 1}));
}

TEST(Checkpoint, TableProperties) {
  auto metadata = [](const std::string& configuration) {
    return R"({"id":"x","configuration":{)" + configuration + "}}";
  };
  EXPECT_EQ(LHTransactions::checkpoint_interval(metadata("")), 10u);
  EXPECT_EQ(LHTransactions::checkpoint_interval(
                metadata(R"("delta.checkpointInterval":"25")")),
            25u);
  EXPECT_EQ(LHTransactions::checkpoint_interval(
                metadata(R"("delta.checkpointInterval":"0")")),
            10u);

  const int64_t day = 24 * 3600 * 1000;
  EXPECT_EQ(LHTransactions::tombstone_retention_ms(metadata("")), 7 * day);
  EXPECT_EQ(LHTransactions::tombstone_retention_ms(metadata(
                R"("delta.deletedFileRetentionDuration":"interval 2 days")")),
            2 * day);
  EXPECT_EQ(LHTransactions::tombstone_retention_ms(metadata(
                R"("delta.deletedFileRetentionDuration":"1 week 12 hours")")),
            7 * day + day / 2);
  EXPECT_EQ(LHTransactions::tombstone_retention_ms(metadata(
                R"("delta.deletedFileRetentionDuration":"soon")")),
            7 * day);
}

TEST(Checkpoint, ProtocolFeatures) {
  EXPECT_EQ(LHTransactions::checkpoint_blocker(
                R"({"minReaderVersion":1,"minWriterVersion":2})"),
            "");
  // tables created by LakeVilla with timestamp_ntz columns
  EXPECT_EQ(
      LHTransactions::checkpoint_blocker(
          R"({"minReaderVersion":3,"minWriterVersion":7,"readerFeatures":["timestampNtz"],"writerFeatures":["timestampNtz","domainMetadata"]})"),
      "");
  EXPECT_NE(
      LHTransactions::checkpoint_blocker(
          R"({"minReaderVersion":3,"minWriterVersion":7,"readerFeatures":["deletionVectors"],"writerFeatures":["deletionVectors"]})"),
      "");
  EXPECT_NE(
      LHTransactions::checkpoint_blocker(
          R"({"minReaderVersion":1,"minWriterVersion":7,"writerFeatures":["rowTracking","domainMetadata"]})"),
      "");
  EXPECT_NE(LHTransactions::checkpoint_blocker(
                R"({"minReaderVersion":4,"minWriterVersion":7})"),
            "");
}

INSTANTIATE_TEST_SUITE_P(Levels, TransactionTest,
                         ::testing::Values("", "0", "1", "2", "01", "02", "12",
                                           "012"),
                         [](const auto& info) {
                           return info.param.empty() ? std::string("none")
                                                     : "L" + info.param;
                         });
