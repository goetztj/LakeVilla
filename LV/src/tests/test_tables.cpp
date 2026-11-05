// Creating and dropping tables (TransactionManagerDeltaLake::create_table,
// drop_table) and the Arrow -> Delta schema conversion; the expected commits
// were written by Spark 3.5.3 / Delta 3.0.0 for
//   CREATE TABLE delta.`s3a://warehouse/wh/ddl.db/<name>/` (...) USING DELTA
#include <gtest/gtest.h>

#include <algorithm>
#include <regex>

#include "Connectors/MemoryConnector.hpp"
#include "Helpers/DeltaSchema.hpp"
#include "LakeVilla/TransactionManager/ManagerDeltaLake.hpp"
#include "test_util.hpp"

using LHTransactions::TransactionManagerDeltaLake;
using StorageConnector::MemoryConnector;
using TableResult = TransactionManagerDeltaLake::TableResult;

namespace {

// (s STRING NOT NULL COMMENT 'the key', b TINYINT, sh SMALLINT, i INT,
//  l BIGINT, f FLOAT, d DOUBLE, bo BOOLEAN, bin BINARY, dt DATE,
//  ts TIMESTAMP, dec DECIMAL(10,2), arr ARRAY<INT>, m MAP<STRING, DOUBLE>,
//  st STRUCT<a: INT NOT NULL, b: ARRAY<STRING>>)
//  COMMENT 'all types' TBLPROPERTIES ('purpose'='test', 'team'='lv')
const std::string SPARK_TYPES = R"spark({"commitInfo":{"timestamp":1792768885721,"operation":"CREATE TABLE","operationParameters":{"isManaged":"false","description":"all types","partitionBy":"[]","properties":"{\"purpose\":\"test\",\"team\":\"lv\"}"},"isolationLevel":"Serializable","isBlindAppend":true,"operationMetrics":{},"engineInfo":"Apache-Spark/3.5.3 Delta-Lake/3.0.0","txnId":"a37441a2-bce0-4de5-8ae9-ac196953161d"}}
{"metaData":{"id":"f627935b-c899-4790-a0e1-2fc47c845e74","description":"all types","format":{"provider":"parquet","options":{}},"schemaString":"{\"type\":\"struct\",\"fields\":[{\"name\":\"s\",\"type\":\"string\",\"nullable\":false,\"metadata\":{\"comment\":\"the key\"}},{\"name\":\"b\",\"type\":\"byte\",\"nullable\":true,\"metadata\":{}},{\"name\":\"sh\",\"type\":\"short\",\"nullable\":true,\"metadata\":{}},{\"name\":\"i\",\"type\":\"integer\",\"nullable\":true,\"metadata\":{}},{\"name\":\"l\",\"type\":\"long\",\"nullable\":true,\"metadata\":{}},{\"name\":\"f\",\"type\":\"float\",\"nullable\":true,\"metadata\":{}},{\"name\":\"d\",\"type\":\"double\",\"nullable\":true,\"metadata\":{}},{\"name\":\"bo\",\"type\":\"boolean\",\"nullable\":true,\"metadata\":{}},{\"name\":\"bin\",\"type\":\"binary\",\"nullable\":true,\"metadata\":{}},{\"name\":\"dt\",\"type\":\"date\",\"nullable\":true,\"metadata\":{}},{\"name\":\"ts\",\"type\":\"timestamp\",\"nullable\":true,\"metadata\":{}},{\"name\":\"dec\",\"type\":\"decimal(10,2)\",\"nullable\":true,\"metadata\":{}},{\"name\":\"arr\",\"type\":{\"type\":\"array\",\"elementType\":\"integer\",\"containsNull\":true},\"nullable\":true,\"metadata\":{}},{\"name\":\"m\",\"type\":{\"type\":\"map\",\"keyType\":\"string\",\"valueType\":\"double\",\"valueContainsNull\":true},\"nullable\":true,\"metadata\":{}},{\"name\":\"st\",\"type\":{\"type\":\"struct\",\"fields\":[{\"name\":\"a\",\"type\":\"integer\",\"nullable\":false,\"metadata\":{}},{\"name\":\"b\",\"type\":{\"type\":\"array\",\"elementType\":\"string\",\"containsNull\":true},\"nullable\":true,\"metadata\":{}}]},\"nullable\":true,\"metadata\":{}}]}","partitionColumns":[],"configuration":{"purpose":"test","team":"lv"},"createdTime":1792768885667}}
{"protocol":{"minReaderVersion":1,"minWriterVersion":2}}
)spark";

// (id INT, t TIMESTAMP_NTZ)
const std::string SPARK_NTZ = R"spark({"commitInfo":{"timestamp":1792768887309,"operation":"CREATE TABLE","operationParameters":{"isManaged":"false","description":null,"partitionBy":"[]","properties":"{}"},"isolationLevel":"Serializable","isBlindAppend":true,"operationMetrics":{},"engineInfo":"Apache-Spark/3.5.3 Delta-Lake/3.0.0","txnId":"df206aa8-bd7c-4ee2-a72c-7bb6a712f021"}}
{"metaData":{"id":"fd0cd06a-fad7-4ad2-b5af-83987466b420","format":{"provider":"parquet","options":{}},"schemaString":"{\"type\":\"struct\",\"fields\":[{\"name\":\"id\",\"type\":\"integer\",\"nullable\":true,\"metadata\":{}},{\"name\":\"t\",\"type\":\"timestamp_ntz\",\"nullable\":true,\"metadata\":{}}]}","partitionColumns":[],"configuration":{},"createdTime":1792768887248}}
{"protocol":{"minReaderVersion":3,"minWriterVersion":7,"readerFeatures":["timestampNtz"],"writerFeatures":["timestampNtz"]}}
)spark";

// (YCSB_KEY VARCHAR(255), FIELD0 STRING)
const std::string SPARK_BASIC = R"spark({"commitInfo":{"timestamp":1792768876444,"operation":"CREATE TABLE","operationParameters":{"isManaged":"false","description":null,"partitionBy":"[]","properties":"{}"},"isolationLevel":"Serializable","isBlindAppend":true,"operationMetrics":{},"engineInfo":"Apache-Spark/3.5.3 Delta-Lake/3.0.0","txnId":"0052d437-0f60-4315-94fb-a22120fa2bd9"}}
{"metaData":{"id":"41950c18-4614-4c73-906a-0a6ee3d29e1e","format":{"provider":"parquet","options":{}},"schemaString":"{\"type\":\"struct\",\"fields\":[{\"name\":\"YCSB_KEY\",\"type\":\"string\",\"nullable\":true,\"metadata\":{\"__CHAR_VARCHAR_TYPE_STRING\":\"varchar(255)\"}},{\"name\":\"FIELD0\",\"type\":\"string\",\"nullable\":true,\"metadata\":{}}]}","partitionColumns":[],"configuration":{},"createdTime":1792768876153}}
{"protocol":{"minReaderVersion":1,"minWriterVersion":2}}
)spark";

std::shared_ptr<arrow::Schema> all_types_schema() {
  return arrow::schema({
      arrow::field("s", arrow::utf8(), false,
                   arrow::key_value_metadata({"comment"}, {"the key"})),
      arrow::field("b", arrow::int8()),
      arrow::field("sh", arrow::int16()),
      arrow::field("i", arrow::int32()),
      arrow::field("l", arrow::int64()),
      arrow::field("f", arrow::float32()),
      arrow::field("d", arrow::float64()),
      arrow::field("bo", arrow::boolean()),
      arrow::field("bin", arrow::binary()),
      arrow::field("dt", arrow::date32()),
      arrow::field("ts", arrow::timestamp(arrow::TimeUnit::MICRO, "UTC")),
      arrow::field("dec", arrow::decimal128(10, 2)),
      arrow::field("arr", arrow::list(arrow::int32())),
      arrow::field("m", arrow::map(arrow::utf8(), arrow::float64())),
      arrow::field("st",
                   arrow::struct_({arrow::field("a", arrow::int32(), false),
                                   arrow::field("b", arrow::list(arrow::utf8()))})),
  });
}

std::shared_ptr<arrow::Schema> ycsb_schema() {
  arrow::FieldVector fields = {arrow::field("YCSB_KEY", arrow::utf8())};
  for (int i = 0; i < 10; i++) {
    fields.push_back(arrow::field("field" + std::to_string(i), arrow::utf8()));
  }
  return arrow::schema(fields);
}

// the values that differ between two creations (ids, times, engine)
std::string normalized(std::string commit) {
  commit = std::regex_replace(commit, std::regex(R"("timestamp":\d+)"),
                              R"("timestamp":0)");
  commit = std::regex_replace(commit, std::regex(R"("createdTime":\d+)"),
                              R"("createdTime":0)");
  commit = std::regex_replace(commit, std::regex(R"("txnId":"[^"]*")"),
                              R"("txnId":"T")");
  commit = std::regex_replace(commit, std::regex(R"("id":"[0-9a-f-]{36}")"),
                              R"("id":"I")");
  commit = std::regex_replace(commit, std::regex(R"("engineInfo":"[^"]*")"),
                              R"("engineInfo":"E")");
  return commit;
}

// metaData.schemaString of a commit
std::string schema_string_of(const std::string& commit) {
  for (auto& line : lvtest::split_lines(commit)) {
    auto doc = LHHelpers::readJSON_rapid(line.c_str());
    if (doc->IsObject() && doc->HasMember("metaData")) {
      return (*doc)["metaData"]["schemaString"].GetString();
    }
  }
  return "";
}

class TablesTest : public ::testing::Test {
 protected:
  MemoryConnector conn;
  std::string path = "wh/ddl.db/t/";

  std::unique_ptr<TransactionManagerDeltaLake> manager(
      LHTransactions::LakeVillaConf levels = {false, false, false},
      uint32_t id = 1) {
    return std::make_unique<TransactionManagerDeltaLake>(levels, path, &conn,
                                                         id);
  }

  std::string version0(const std::string& table) {
    return conn.get(table + "_delta_log/" + lvtest::log_name(0))
        .value_or("");
  }
};

}  // namespace

TEST(DeltaSchema, MatchesSpark) {
  auto schema = LHHelpers::to_delta_schema(*all_types_schema());
  ASSERT_TRUE(schema.has_value());
  EXPECT_EQ(schema->schema_string, schema_string_of(SPARK_TYPES));
  EXPECT_TRUE(schema->features.empty());
}

TEST(DeltaSchema, FieldMetadataIsKept) {
  auto schema = LHHelpers::to_delta_schema(*arrow::schema(
      {arrow::field("YCSB_KEY", arrow::utf8(), true,
                    arrow::key_value_metadata({"__CHAR_VARCHAR_TYPE_STRING"},
                                              {"varchar(255)"})),
       arrow::field("FIELD0", arrow::utf8())}));
  ASSERT_TRUE(schema.has_value());
  EXPECT_EQ(schema->schema_string, schema_string_of(SPARK_BASIC));
}

TEST(DeltaSchema, TimestampWithoutTimeZoneNeedsFeature) {
  auto schema = LHHelpers::to_delta_schema(*arrow::schema(
      {arrow::field("id", arrow::int32()),
       arrow::field("t", arrow::timestamp(arrow::TimeUnit::MICRO))}));
  ASSERT_TRUE(schema.has_value());
  EXPECT_EQ(schema->schema_string, schema_string_of(SPARK_NTZ));
  EXPECT_EQ(schema->features, std::vector<std::string>{"timestampNtz"});
}

TEST(DeltaSchema, OtherArrowTypes) {
  auto schema = LHHelpers::to_delta_schema(*arrow::schema(
      {arrow::field("a", arrow::large_utf8()),
       arrow::field("b", arrow::large_binary()),
       arrow::field("c", arrow::dictionary(arrow::int32(), arrow::utf8())),
       arrow::field("d", arrow::list(arrow::field("x", arrow::int64(), false))),
       arrow::field("e", arrow::fixed_size_binary(16))}));
  ASSERT_TRUE(schema.has_value());
  EXPECT_EQ(schema->schema_string,
            R"({"type":"struct","fields":[)"
            R"({"name":"a","type":"string","nullable":true,"metadata":{}},)"
            R"({"name":"b","type":"binary","nullable":true,"metadata":{}},)"
            R"({"name":"c","type":"string","nullable":true,"metadata":{}},)"
            R"({"name":"d","type":{"type":"array","elementType":"long","containsNull":false},"nullable":true,"metadata":{}},)"
            R"({"name":"e","type":"binary","nullable":true,"metadata":{}}]})");
}

TEST(DeltaSchema, RejectsWhatDeltaCannotRepresent) {
  auto rejected = [](arrow::FieldVector fields) {
    std::string error;
    bool ok = LHHelpers::to_delta_schema(*arrow::schema(fields), &error)
                  .has_value();
    EXPECT_FALSE(error.empty());
    return !ok;
  };
  EXPECT_TRUE(rejected({}));
  EXPECT_TRUE(rejected({arrow::field("u", arrow::uint32())}));
  EXPECT_TRUE(rejected({arrow::field("t", arrow::time64(arrow::TimeUnit::MICRO))}));
  EXPECT_TRUE(rejected({arrow::field("a", arrow::int32()),
                        arrow::field("A", arrow::int32())}));
  EXPECT_TRUE(rejected({arrow::field("a b", arrow::int32())}));
  EXPECT_TRUE(rejected({arrow::field("x=1", arrow::int32())}));
  EXPECT_TRUE(rejected({arrow::field(
      "st", arrow::struct_({arrow::field("q", arrow::int32()),
                            arrow::field("Q", arrow::int32())}))}));
  EXPECT_TRUE(rejected({arrow::field("st", arrow::struct_({}))}));
}

TEST(DeltaSchema, RandomUuid) {
  std::regex uuid("[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}");
  auto a = LHHelpers::random_uuid();
  EXPECT_TRUE(std::regex_match(a, uuid)) << a;
  EXPECT_NE(a, LHHelpers::random_uuid());
}

TEST_F(TablesTest, CreateWritesSparkCommit) {
  TransactionManagerDeltaLake::TableOptions options;
  options.description = "all types";
  options.properties = {{"purpose", "test"}, {"team", "lv"}};
  EXPECT_EQ(manager()->create_table(path, *all_types_schema(), options),
            TableResult::Ok);
  EXPECT_EQ(conn.keys(path), std::vector<std::string>{
                                 path + "_delta_log/" + lvtest::log_name(0)});
  EXPECT_EQ(normalized(version0(path)), normalized(SPARK_TYPES));
}

TEST_F(TablesTest, CreateWithTableFeature) {
  auto schema = arrow::schema(
      {arrow::field("id", arrow::int32()),
       arrow::field("t", arrow::timestamp(arrow::TimeUnit::MICRO))});
  EXPECT_EQ(manager()->create_table("wh/ddl.db/ntz", *schema),
            TableResult::Ok);
  EXPECT_EQ(normalized(version0("wh/ddl.db/ntz/")), normalized(SPARK_NTZ));
}

TEST_F(TablesTest, CreateRefusesExistingAndInvalid) {
  auto tm = manager();
  ASSERT_EQ(tm->create_table(path, *ycsb_schema()), TableResult::Ok);
  auto before = version0(path);
  EXPECT_EQ(tm->create_table(path, *ycsb_schema()), TableResult::AlreadyExists);
  EXPECT_EQ(version0(path), before);

  conn.put("wh/ddl.db/files/data.csv", "1,2");
  EXPECT_EQ(tm->create_table("wh/ddl.db/files/", *ycsb_schema()),
            TableResult::NotEmpty);
  EXPECT_FALSE(conn.check("wh/ddl.db/files/_delta_log/" + lvtest::log_name(0)));

  EXPECT_EQ(tm->create_table("", *ycsb_schema()), TableResult::Invalid);
  EXPECT_EQ(tm->create_table("/abs/", *ycsb_schema()), TableResult::Invalid);
  std::string error;
  EXPECT_EQ(tm->create_table("wh/ddl.db/u/",
                             *arrow::schema({arrow::field("u", arrow::uint8())}),
                             {}, &error),
            TableResult::Invalid);
  EXPECT_NE(error.find("uint8"), std::string::npos) << error;
  for (std::string key : {"delta.appendOnly", "comment", "Location", ""}) {
    TransactionManagerDeltaLake::TableOptions options;
    options.properties = {{key, "x"}};
    EXPECT_EQ(tm->create_table("wh/ddl.db/p/", *ycsb_schema(), options),
              TableResult::Invalid)
        << key;
  }
  EXPECT_TRUE(conn.keys("wh/ddl.db/u/").empty());
  EXPECT_TRUE(conn.keys("wh/ddl.db/p/").empty());
}

TEST_F(TablesTest, ConcurrentCreateHasOneWinner) {
  // another client creates the table between the check and the write
  bool injected = false;
  conn.fail_hook = [&](MemoryConnector::Op op, const std::string& key) {
    if (!injected && op == MemoryConnector::Op::WriteAtomic &&
        key == path + "_delta_log/" + lvtest::log_name(0)) {
      injected = true;
      conn.put(key, "{\"commitInfo\":{}}\n");
    }
    return false;
  };
  EXPECT_EQ(manager()->create_table(path, *ycsb_schema()),
            TableResult::AlreadyExists);
  conn.fail_hook = nullptr;
  EXPECT_EQ(version0(path), "{\"commitInfo\":{}}\n");
}

TEST_F(TablesTest, CreatedTableIsUsable) {
  for (auto levels : std::vector<LHTransactions::LakeVillaConf>{
           {false, false, false}, {true, true, false}, {false, false, true},
           {true, true, true}}) {
    conn.clear();
    ASSERT_EQ(manager()->create_table(path, *ycsb_schema()), TableResult::Ok);
    for (int i = 0; i < 2; i++) {
      auto tm = manager(levels, 10 + i);
      ASSERT_TRUE(tm->begin_transaction_ycsb());
      ASSERT_TRUE(tm->add_file(0, lvtest::ycsb_table("user" + std::to_string(i)),
                               "user" + std::to_string(i)));
      ASSERT_TRUE(tm->commit(false));
    }
    auto tm = manager(levels, 20);
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    auto keys = lvtest::keys_of(tm->read_table(0));
    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(keys, (std::vector<std::string>{"user0", "user1"}));
    tm->commit(true);
  }
}

TEST_F(TablesTest, DropRemovesTableOnly) {
  ASSERT_EQ(manager()->create_table(path, *ycsb_schema()), TableResult::Ok);
  std::string sibling = "wh/ddl.db/t2/";
  ASSERT_EQ(manager()->create_table(sibling, *ycsb_schema()), TableResult::Ok);
  {
    auto tm = manager({false, false, true}, 2);
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    ASSERT_TRUE(tm->add_file(0, lvtest::ycsb_table("user1"), "user1"));
    ASSERT_TRUE(tm->commit(false));
  }
  ASSERT_GT(conn.keys(path).size(), 2u);
  ASSERT_NE(conn.get(LV_DEFAULT_GLOBAL_LOG)->find(path), std::string::npos);

  // the log goes first
  std::vector<std::string> deleted;
  conn.fail_hook = [&](MemoryConnector::Op op, const std::string& key) {
    if (op == MemoryConnector::Op::Delete && key.rfind(path, 0) == 0) {
      deleted.push_back(key);
    }
    return false;
  };
  EXPECT_EQ(manager()->drop_table("wh/ddl.db/t"), TableResult::Ok);
  conn.fail_hook = nullptr;
  EXPECT_TRUE(conn.keys(path).empty());
  EXPECT_EQ(conn.keys(sibling).size(), 1u);
  EXPECT_EQ(conn.get(LV_DEFAULT_GLOBAL_LOG)->find(path), std::string::npos);
  // version 0 first, the data file, the other versions last
  ASSERT_EQ(deleted.size(), 3u);
  EXPECT_EQ(deleted[0], path + "_delta_log/" + lvtest::log_name(0));
  EXPECT_EQ(deleted[1].find("_delta_log/"), std::string::npos);
  EXPECT_EQ(deleted[2], path + "_delta_log/" + lvtest::log_name(1));

  EXPECT_EQ(manager()->drop_table(path), TableResult::NotFound);
  conn.put("wh/ddl.db/files/data.csv", "1,2");
  EXPECT_EQ(manager()->drop_table("wh/ddl.db/files/"), TableResult::NotEmpty);
  EXPECT_TRUE(conn.check("wh/ddl.db/files/data.csv"));
  EXPECT_EQ(manager()->drop_table("/"), TableResult::Invalid);
}

TEST_F(TablesTest, InterruptedDropCanBeCompleted) {
  ASSERT_EQ(manager()->create_table(path, *ycsb_schema()), TableResult::Ok);
  {
    auto tm = manager({false, false, false}, 2);
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    ASSERT_TRUE(tm->add_file(0, lvtest::ycsb_table("user1"), "user1"));
    ASSERT_TRUE(tm->commit(false));
  }
  int deletes = 0;
  conn.fail_hook = [&](MemoryConnector::Op op, const std::string&) {
    return op == MemoryConnector::Op::Delete && ++deletes > 1;
  };
  auto tm = manager();
  tm->set_storage_retries(1);
  EXPECT_EQ(tm->drop_table(path), TableResult::Error);
  conn.fail_hook = nullptr;
  // version 0 is gone (and there is no checkpoint): no reader can
  // reconstruct a snapshot; the rest of the log is left
  EXPECT_FALSE(conn.check(path + "_delta_log/" + lvtest::log_name(0)));
  EXPECT_TRUE(conn.check(path + "_delta_log/" + lvtest::log_name(1)));
  // a second drop completes it
  EXPECT_EQ(tm->drop_table(path), TableResult::Ok);
  EXPECT_TRUE(conn.keys(path).empty());
}

// LV[Read]: a table recreated at the same path must not inherit the old
// table's global-log entry
TEST_F(TablesTest, RecreatedTableWithGlobalLog) {
  LHTransactions::LakeVillaConf read = {false, false, true};
  auto insert = [&](const std::string& key, uint32_t id) {
    auto tm = manager(read, id);
    if (!tm->begin_transaction_ycsb()) {
      return false;
    }
    if (!tm->add_file(0, lvtest::ycsb_table(key), key)) {
      return false;
    }
    return tm->commit(false);
  };
  ASSERT_EQ(manager()->create_table(path, *ycsb_schema()), TableResult::Ok);
  for (int i = 0; i < 3; i++) {
    ASSERT_TRUE(insert("old" + std::to_string(i), 1 + i));
  }
  ASSERT_EQ(*conn.get(LV_DEFAULT_GLOBAL_LOG), path + " 3\n");

  // dropped with drop_table
  ASSERT_EQ(manager()->drop_table(path), TableResult::Ok);
  ASSERT_EQ(manager()->create_table(path, *ycsb_schema()), TableResult::Ok);
  EXPECT_TRUE(insert("new0", 10));
  EXPECT_EQ(*conn.get(LV_DEFAULT_GLOBAL_LOG), path + " 1\n");

  // removed by other means: create_table removes the stale entry
  for (auto& key : conn.keys(path)) {
    conn.delete_file(key);
  }
  ASSERT_EQ(manager()->create_table(path, *ycsb_schema()), TableResult::Ok);
  EXPECT_TRUE(insert("new1", 11));
  EXPECT_EQ(*conn.get(LV_DEFAULT_GLOBAL_LOG), path + " 1\n");
  auto tm = manager(read, 12);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  EXPECT_EQ(lvtest::keys_of(tm->read_table(0)), std::vector<std::string>{"new1"});
  tm->commit(true);
}

// the schema of Spark's metaData parses and is written again unchanged
TEST(Tables, SparkSchemaStringRoundTrip) {
  for (auto* commit : {&SPARK_TYPES, &SPARK_NTZ, &SPARK_BASIC}) {
    std::string metadata;
    for (auto& line : lvtest::split_lines(*commit)) {
      auto doc = LHHelpers::readJSON_rapid(line.c_str());
      if (doc->HasMember("metaData")) {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        (*doc)["metaData"].Accept(writer);
        metadata = buffer.GetString();
      }
    }
    std::string error;
    auto schema = LHHelpers::table_schema(metadata, &error);
    ASSERT_TRUE(schema.has_value()) << error;
    auto doc = LHHelpers::readJSON_rapid(metadata.c_str());
    auto delta = LHHelpers::to_delta_schema(*schema->schema);
    ASSERT_TRUE(delta.has_value());
    EXPECT_EQ(delta->schema_string, (*doc)["schemaString"].GetString());
  }
}
