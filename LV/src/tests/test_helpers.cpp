#include <gtest/gtest.h>

#include "Helpers/Helper.hpp"
#include "test_util.hpp"

TEST(Helpers, ParquetRoundTrip) {
  auto table = lvtest::ycsb_table("user42", "value");
  auto encoded = LHHelpers::encodeAsParquet(table);
  ASSERT_GT(encoded->second, 0u);

  auto decoded = LHHelpers::readParquetAsTable(encoded->first, encoded->second);
  ASSERT_NE(decoded, nullptr);
  EXPECT_TRUE(decoded->Equals(*table));
  EXPECT_EQ(lvtest::keys_of(decoded), std::vector<std::string>{"user42"});
}

TEST(Helpers, EncodeAsStringMatchesEncode) {
  auto table = lvtest::ycsb_table("k");
  auto as_string = LHHelpers::encodeAsParquet_str(table);
  auto decoded =
      LHHelpers::readParquetAsTable(as_string.data(), as_string.size());
  ASSERT_NE(decoded, nullptr);
  EXPECT_TRUE(decoded->Equals(*table));
}

TEST(Helpers, ReadInvalidParquetReturnsNull) {
  std::string garbage = "this is not parquet";
  EXPECT_EQ(LHHelpers::readParquetAsTable(garbage.data(), garbage.size()),
            nullptr);
  EXPECT_EQ(LHHelpers::readParquetAsTable(nullptr, 0), nullptr);
  EXPECT_EQ(LHHelpers::readParquet(nullptr, 0), "");
}

TEST(Helpers, ReadParquetAsText) {
  auto table = lvtest::ycsb_table("user1", "abc");
  auto bytes = LHHelpers::encodeAsParquet_str(table);
  auto text = LHHelpers::readParquet(bytes.data(), bytes.size());
  EXPECT_NE(text.find("user1"), std::string::npos);
  EXPECT_NE(text.find("abc"), std::string::npos);
}

TEST(Helpers, EncodeIntColumns) {
  auto encoded = LHHelpers::encodeAsParquet({"x", "y"}, {1, 2, 3, 4});
  auto decoded = LHHelpers::readParquetAsTable(encoded->first, encoded->second);
  ASSERT_NE(decoded, nullptr);
  EXPECT_EQ(decoded->num_rows(), 2);
  EXPECT_EQ(decoded->num_columns(), 2);
}

TEST(Helpers, JsonRoundTrip) {
  auto doc = LHHelpers::readJSON_rapid(R"({"a":1,"b":"x"})");
  ASSERT_TRUE(doc->IsObject());
  auto buffer = LHHelpers::encodeAsJSON_rapid(doc);
  EXPECT_EQ(std::string(buffer.GetString()), R"({"a":1,"b":"x"})");
}

// the checkpoint written by LakeVilla follows the Delta checkpoint schema
// (add.path string, maps for partitionValues/configuration, protocol and
// metaData rows) and can be read back
TEST(Helpers, CheckpointRoundTrip) {
  LHHelpers::CheckpointContent content;
  content.protocol = R"({"minReaderVersion":1,"minWriterVersion":2})";
  content.metadata =
      R"({"id":"t","format":{"provider":"parquet","options":{}},"schemaString":"{}","partitionColumns":[],"configuration":{"delta.appendOnly":"false"},"createdTime":1})";
  content.adds = {{"a.parquet", R"({"numRecords":1})", 10},
                  {"b.parquet", "", 20}};
  content.removes = {{"old.parquet", 1700000000000}};

  auto checkpoint = LHHelpers::generateCheckpoint(content);
  auto table =
      LHHelpers::readParquetAsTable(checkpoint->first, checkpoint->second);
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->num_rows(), 5);

  auto add_type = std::static_pointer_cast<arrow::StructType>(
      table->GetColumnByName("add")->type());
  EXPECT_TRUE(add_type->GetFieldByName("path")->type()->Equals(arrow::utf8()));
  EXPECT_EQ(add_type->GetFieldByName("partitionValues")->type()->id(),
            arrow::Type::MAP);
  auto meta_type = std::static_pointer_cast<arrow::StructType>(
      table->GetColumnByName("metaData")->type());
  EXPECT_EQ(meta_type->GetFieldByName("configuration")->type()->id(),
            arrow::Type::MAP);

  auto read = LHHelpers::readDLCheckpointContent(checkpoint->first,
                                                 checkpoint->second);
  ASSERT_TRUE(read.has_value());
  ASSERT_EQ(read->adds.size(), 2u);
  EXPECT_EQ(read->adds[0].path, "a.parquet");
  EXPECT_EQ(read->adds[0].stats, R"({"numRecords":1})");
  EXPECT_EQ(read->adds[1].size, 20);
  ASSERT_EQ(read->removes.size(), 1u);
  EXPECT_EQ(read->removes[0].path, "old.parquet");
  EXPECT_EQ(read->protocol, content.protocol);
  EXPECT_NE(read->metadata.find(R"("configuration":{"delta.appendOnly":"false"})"),
            std::string::npos);
  EXPECT_TRUE(read->unsupported.empty());

  EXPECT_EQ(LHHelpers::readDLCheckpoint(checkpoint->first, checkpoint->second),
            (std::vector<std::string>{"a.parquet", "b.parquet"}));
}

TEST(Helpers, ComputeStatistics) {
  arrow::Int64Builder ints;
  ASSERT_TRUE(ints.AppendValues({5, -3, 12}).ok());
  arrow::DoubleBuilder doubles;
  ASSERT_TRUE(doubles.Append(1.5).ok());
  ASSERT_TRUE(doubles.AppendNull().ok());
  ASSERT_TRUE(doubles.Append(-0.25).ok());
  arrow::StringBuilder strings;
  ASSERT_TRUE(strings.AppendValues({"b", "a", "c"}).ok());
  arrow::Date32Builder dates;
  ASSERT_TRUE(dates.AppendValues({1, 2, 3}).ok());

  std::shared_ptr<arrow::Array> a, b, c, d;
  ASSERT_TRUE(ints.Finish(&a).ok());
  ASSERT_TRUE(doubles.Finish(&b).ok());
  ASSERT_TRUE(strings.Finish(&c).ok());
  ASSERT_TRUE(dates.Finish(&d).ok());
  auto table = arrow::Table::Make(
      arrow::schema({arrow::field("i", arrow::int64()),
                     arrow::field("d", arrow::float64()),
                     arrow::field("s", arrow::utf8()),
                     arrow::field("t", arrow::date32())}),
      {a, b, c, d});

  auto stats = LHHelpers::computeStatistics(*table);
  EXPECT_EQ(stats.num_records, 3);
  ASSERT_EQ(stats.columns.size(), 4u);

  EXPECT_TRUE(stats.columns[0].numeric);
  EXPECT_EQ(stats.columns[0].min, "-3");
  EXPECT_EQ(stats.columns[0].max, "12");

  EXPECT_EQ(stats.columns[1].min, "-0.25");
  EXPECT_EQ(stats.columns[1].max, "1.5");
  EXPECT_EQ(stats.columns[1].null_count, 1);

  EXPECT_FALSE(stats.columns[2].numeric);
  EXPECT_EQ(stats.columns[2].min, "a");
  EXPECT_EQ(stats.columns[2].max, "c");

  // no min/max for unsupported types: engines then never skip the file
  EXPECT_FALSE(stats.columns[3].has_min_max);
}

TEST(Helpers, ParquetNumRows) {
  auto bytes = LHHelpers::encodeAsParquet_str(lvtest::ycsb_table("k"));
  EXPECT_EQ(LHHelpers::parquetNumRows(bytes.data(), bytes.size()), 1);
  std::string garbage = "nope";
  EXPECT_EQ(LHHelpers::parquetNumRows(garbage.data(), garbage.size()), -1);
}

// tables with several chunks and types without a dedicated printer
TEST(Helpers, ReadParquetAllChunksAndTypes) {
  auto t1 = lvtest::ycsb_table("first", "x");
  auto t2 = lvtest::ycsb_table("second", "y");
  auto table = *arrow::ConcatenateTables({t1, t2});
  ASSERT_GT(table->column(0)->num_chunks(), 1);

  auto bytes = LHHelpers::encodeAsParquet_str(table);
  auto text = LHHelpers::readParquet(bytes.data(), bytes.size());
  EXPECT_NE(text.find("first"), std::string::npos);
  EXPECT_NE(text.find("second"), std::string::npos);

  arrow::ListBuilder list(arrow::default_memory_pool(),
                          std::make_shared<arrow::Int32Builder>());
  ASSERT_TRUE(list.Append().ok());
  ASSERT_TRUE(
      static_cast<arrow::Int32Builder*>(list.value_builder())->Append(7).ok());
  std::shared_ptr<arrow::Array> lists;
  ASSERT_TRUE(list.Finish(&lists).ok());
  auto nested = arrow::Table::Make(
      arrow::schema({arrow::field("l", arrow::list(arrow::int32()))}),
      {lists});
  auto nested_bytes = LHHelpers::encodeAsParquet_str(nested);
  auto nested_text =
      LHHelpers::readParquet(nested_bytes.data(), nested_bytes.size());
  EXPECT_NE(nested_text.find("7"), std::string::npos);
}
