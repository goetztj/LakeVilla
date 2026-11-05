// Integration tests against a real S3 endpoint (e.g. the MinIO container of
// docker-compose.yml). Skipped unless LV_TEST_S3_ENDPOINT is set:
//
//   LV_TEST_S3_ENDPOINT=localhost:9000 LV_TEST_S3_USER=admin \
//   LV_TEST_S3_PASSWD=password LV_TEST_S3_BUCKET=warehouse \
//   ./lakevilla-s3-tests
#include <aws/core/Aws.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <random>
#include <thread>

#include "Connectors/MinIOConnector.hpp"
#include "LakeVilla/TransactionManager/ManagerDeltaLake.hpp"
#include "test_util.hpp"

using StorageConnector::MinIOConnector;
using StorageConnector::WriteStatus;

namespace {

std::string env(const char* name, const char* fallback) {
  auto value = std::getenv(name);
  return value ? value : fallback;
}

class S3Test : public ::testing::Test {
 protected:
  std::unique_ptr<MinIOConnector> conn;
  std::string prefix;

  void SetUp() override {
    if (!std::getenv("LV_TEST_S3_ENDPOINT")) {
      GTEST_SKIP() << "LV_TEST_S3_ENDPOINT not set";
    }
    StorageConnector::MinIOConfig config;
    config.endpointURI = env("LV_TEST_S3_ENDPOINT", "");
    config.user = env("LV_TEST_S3_USER", "admin");
    config.passwd = env("LV_TEST_S3_PASSWD", "password");
    config.region = env("LV_TEST_S3_REGION", "us-east-1");
    config.default_bucket = env("LV_TEST_S3_BUCKET", "warehouse");
    conn = std::make_unique<MinIOConnector>(&config);

    std::random_device rd;
    prefix = "lv-tests/" + std::to_string(rd()) + "/";
  }

  void TearDown() override {
    if (!conn) {
      return;
    }
    auto objects = conn->list_objects(prefix);
    if (objects) {
      for (auto& obj : *objects) {
        conn->delete_file(obj.key);
      }
    }
  }
};

}  // namespace

TEST_F(S3Test, WriteReadCheckDelete) {
  std::string data = "hello s3";
  ASSERT_TRUE(conn->write(prefix + "a", data.data(), data.size()));
  EXPECT_TRUE(conn->check(prefix + "a"));
  auto content = conn->read2(prefix + "a");
  ASSERT_NE(content->data, nullptr);
  EXPECT_EQ(std::string(content->data, content->length), data);
  EXPECT_TRUE(conn->delete_file(prefix + "a"));
  EXPECT_FALSE(conn->check(prefix + "a"));
  EXPECT_EQ(conn->read2(prefix + "a")->data, nullptr);
}

TEST_F(S3Test, BinaryDataRoundTrip) {
  std::string data("a\0b\xff\n", 5);
  ASSERT_TRUE(conn->write(prefix + "bin", data.data(), data.size()));
  auto content = conn->read2(prefix + "bin");
  ASSERT_NE(content->data, nullptr);
  EXPECT_EQ(std::string(content->data, content->length), data);
}

TEST_F(S3Test, ConditionalWrite) {
  std::string first = "first", second = "second";
  EXPECT_EQ(conn->write_atomic(prefix + "k", first.data(), first.size()),
            WriteStatus::Ok);
  EXPECT_EQ(conn->write_atomic(prefix + "k", second.data(), second.size()),
            WriteStatus::AlreadyExists);
  auto content = conn->read2(prefix + "k");
  EXPECT_EQ(std::string(content->data, content->length), first);
}

TEST_F(S3Test, ConditionalOverwrite) {
  std::string marker = "marker", commit = "commit", takeover = "takeover";
  auto key = prefix + "marker";
  ASSERT_EQ(conn->write_atomic(key, marker.data(), marker.size()),
            WriteStatus::Ok);
  StorageConnector::ObjectVersion version;
  auto content = conn->read_versioned(key, version);
  auto etag = version.etag;
  ASSERT_NE(content->data, nullptr);
  ASSERT_FALSE(etag.empty());

  EXPECT_EQ(conn->write_if_match(key, commit.data(), commit.size(),
                                 "\"deadbeef\""),
            WriteStatus::PreconditionFailed);
  EXPECT_EQ(conn->write_if_match(key, commit.data(), commit.size(), etag),
            WriteStatus::Ok);
  EXPECT_EQ(conn->write_if_match(key, takeover.data(), takeover.size(), etag),
            WriteStatus::PreconditionFailed);
  auto final_content = conn->read2(key);
  EXPECT_EQ(std::string(final_content->data, final_content->length), commit);
  EXPECT_EQ(conn->write_if_match(prefix + "missing", commit.data(),
                                 commit.size(), etag),
            WriteStatus::PreconditionFailed);
}

TEST_F(S3Test, StoreClock) {
  // the first estimate has an uncertainty of up to half a second
  auto first = conn->store_time();
  EXPECT_LE(first.uncertainty_ms, 1000);
  // repeated samples across second boundaries narrow it
  for (int i = 0; i < 12; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(270));
    conn->store_time();
  }
  auto t = conn->store_time();
  auto local = StorageConnector::local_time_ms();
  std::cout << "store clock: offset " << t.now_ms - local << " ms, +- "
            << t.uncertainty_ms << " ms" << std::endl;
  EXPECT_LT(t.uncertainty_ms, first.uncertainty_ms + 1);
  EXPECT_LT(t.uncertainty_ms, 300);
  // MinIO runs on the same host: the offset is about 0
  EXPECT_LE(std::llabs(t.now_ms - local), t.uncertainty_ms + 50);
  // the marker's Last-Modified is reported in store time
  std::string data = "x";
  ASSERT_TRUE(conn->write(prefix + "lm", data.data(), data.size()));
  StorageConnector::ObjectVersion version;
  conn->read_versioned(prefix + "lm", version);
  EXPECT_LE(std::llabs(version.last_modified_ms - local), 2000);
}

TEST_F(S3Test, ListingIsPaginated) {
  const int n = 1050;
  std::string data = "x";
  for (int i = 0; i < n; i++) {
    ASSERT_TRUE(conn->write(prefix + "many/" + std::to_string(i), data.data(),
                            data.size()));
  }
  auto objects = conn->list_objects(prefix + "many/");
  ASSERT_TRUE(objects.has_value());
  EXPECT_EQ(objects->size(), size_t(n));

  auto empty = conn->list_objects(prefix + "nothing/");
  ASSERT_TRUE(empty.has_value());
  EXPECT_TRUE(empty->empty());
}

TEST_F(S3Test, TransactionRoundTrip) {
  for (std::string levels : {"01", "2", "012"}) {
    std::string base = prefix + "table" + levels + "/";
    lvtest::create_ycsb_table(*conn, base, {"user0", "user1"});

    LHTransactions::LakeVillaConf conf = {false, false, false};
    for (char c : levels) conf[c - '0'] = true;

    LHTransactions::TransactionManagerDeltaLake tm(conf, base, conn.get(), 1);
    tm.set_global_log_paths(prefix + "global/log.txt",
                            prefix + "global/log.txt.tmp");
    ASSERT_TRUE(tm.begin_transaction_ycsb()) << levels;
    ASSERT_TRUE(tm.add_file(0, lvtest::ycsb_table("user2"), "user2"));
    ASSERT_TRUE(tm.remove_file(0, std::string("user0")));
    ASSERT_TRUE(tm.commit(false)) << levels;

    LHTransactions::TransactionManagerDeltaLake reader(conf, base, conn.get(),
                                                       2);
    reader.set_global_log_paths(prefix + "global/log.txt",
                                prefix + "global/log.txt.tmp");
    ASSERT_TRUE(reader.begin_transaction_ycsb());
    EXPECT_EQ(lvtest::keys_of(reader.read_table(0)),
              (std::vector<std::string>{"user1", "user2"}))
        << levels;
    reader.commit(true);
  }
}

// Writes a LakeVilla checkpoint of an existing table (e.g. one created by
// Spark) to check that other engines can read it. Skipped unless
// LV_TEST_CHECKPOINT_TABLE is set (path inside the bucket, ending with '/').
TEST_F(S3Test, CheckpointExistingTable) {
  auto table = std::getenv("LV_TEST_CHECKPOINT_TABLE");
  if (!table) {
    GTEST_SKIP() << "LV_TEST_CHECKPOINT_TABLE not set";
  }
  std::string base = table;
  LHTransactions::TransactionManagerDeltaLake tm({true, true, false}, base,
                                                 conn.get(), 4242);
  ASSERT_TRUE(tm.begin_transaction_ycsb());
  auto rows = tm.read_table(0);
  ASSERT_NE(rows, nullptr);
  ASSERT_TRUE(tm.create_checkpoint(0));
  tm.commit(true);

  auto last = conn->read2(base + "_delta_log/_last_checkpoint");
  ASSERT_NE(last->data, nullptr);
  std::cout << "rows: " << rows->num_rows() << ", _last_checkpoint: "
            << std::string(last->data, last->length) << std::endl;
}

// Creates a table and commits ten inserts with LakeVilla: the commit of
// version 10 writes a checkpoint (default interval), which other engines
// must be able to read after a log cleanup. Skipped unless
// LV_TEST_CHECKPOINT_COMMITS_TABLE is set (a new path inside the bucket,
// ending with '/').
TEST_F(S3Test, CommitsCheckpointTableForSpark) {
  auto table = std::getenv("LV_TEST_CHECKPOINT_COMMITS_TABLE");
  if (!table) {
    GTEST_SKIP() << "LV_TEST_CHECKPOINT_COMMITS_TABLE not set";
  }
  std::string base = table;
  {
    LHTransactions::TransactionManagerDeltaLake tm({true, true, false}, base,
                                                   conn.get(), 4300);
    ASSERT_EQ(tm.create_table(base, *lvtest::ycsb_table("user0")->schema()),
              LHTransactions::TransactionManagerDeltaLake::TableResult::Ok);
  }
  for (int i = 0; i < 10; i++) {
    LHTransactions::TransactionManagerDeltaLake tm({true, true, false}, base,
                                                   conn.get(), 4301 + i);
    ASSERT_TRUE(tm.begin_transaction_ycsb());
    auto key = "user" + std::to_string(i);
    ASSERT_TRUE(tm.add_file(0, lvtest::ycsb_table(key, "v" + key), key));
    ASSERT_TRUE(tm.commit(false));
  }

  EXPECT_TRUE(conn->check(base + "_delta_log/" +
                          LHTransactions::log_file_name(
                              10, ".checkpoint.parquet")));
  auto last = conn->read2(base + "_delta_log/_last_checkpoint");
  ASSERT_NE(last->data, nullptr);
  std::cout << "_last_checkpoint: " << std::string(last->data, last->length)
            << std::endl;
}

TEST_F(S3Test, ExpiredMarkerIsTakenOver) {
  std::string base = prefix + "leased/";
  lvtest::create_ycsb_table(*conn, base, {"user0"});
  // marker of a crashed client; its lease ended 0.2 s from now
  auto deadline = conn->store_time().now_ms + 200;
  std::string marker =
      R"({"commitInfo":{"txnId":77,"operation":"WRITE"}})" "\n"
      R"({"marker":{"workDir":"x/","running":true,"dependencies":[],"txnid":77,"deadline":)" +
      std::to_string(deadline) + "}}";
  auto v1 = base + "_delta_log/" + lvtest::log_name(1);
  ASSERT_TRUE(conn->write(v1, marker.data(), marker.size()));

  LHTransactions::ProtocolOptions options;
  options.marker_lease_ms = 300;
  options.marker_grace_ms = 100;
  LHTransactions::TransactionManagerDeltaLake tm({true, true, false}, base,
                                                 conn.get(), 1);
  tm.set_protocol_options(options);
  ASSERT_TRUE(tm.begin_transaction_ycsb());
  ASSERT_TRUE(tm.add_file(0, lvtest::ycsb_table("user1"), "user1"));
  EXPECT_TRUE(tm.commit(false));

  auto content = conn->read2(v1);
  ASSERT_NE(content->data, nullptr);
  EXPECT_NE(std::string(content->data, content->length).find("MarkerTakeover"),
            std::string::npos);

  LHTransactions::TransactionManagerDeltaLake reader({false, false, false},
                                                     base, conn.get(), 2);
  ASSERT_TRUE(reader.begin_transaction_ycsb());
  EXPECT_EQ(lvtest::keys_of(reader.read_table(0)),
            (std::vector<std::string>{"user0", "user1"}));
  reader.commit(true);
}

TEST_F(S3Test, CreateWriteDropTable) {
  std::string base = prefix + "ddl/";
  arrow::FieldVector fields = {arrow::field("YCSB_KEY", arrow::utf8())};
  for (int i = 0; i < 10; i++) {
    fields.push_back(arrow::field("field" + std::to_string(i), arrow::utf8()));
  }
  LHTransactions::TransactionManagerDeltaLake tm({true, true, false}, base,
                                                 conn.get(), 1);
  using TableResult = LHTransactions::TransactionManagerDeltaLake::TableResult;
  ASSERT_EQ(tm.create_table(base, *arrow::schema(fields)), TableResult::Ok);
  EXPECT_EQ(tm.create_table(base, *arrow::schema(fields)),
            TableResult::AlreadyExists);

  ASSERT_TRUE(tm.begin_transaction_ycsb());
  ASSERT_TRUE(tm.add_file(0, lvtest::ycsb_table("user1"), "user1"));
  ASSERT_TRUE(tm.commit(false));
  auto objects = conn->list_objects(base);
  ASSERT_TRUE(objects.has_value());
  EXPECT_GE(objects->size(), 3u);  // two versions and a data file

  LHTransactions::TransactionManagerDeltaLake dropper({false, false, false},
                                                      base, conn.get(), 2);
  EXPECT_EQ(dropper.drop_table(base), TableResult::Ok);
  objects = conn->list_objects(base);
  ASSERT_TRUE(objects.has_value());
  EXPECT_TRUE(objects->empty());
  EXPECT_EQ(dropper.drop_table(base), TableResult::NotFound);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  int result = RUN_ALL_TESTS();
  Aws::ShutdownAPI(options);
  return result;
}
