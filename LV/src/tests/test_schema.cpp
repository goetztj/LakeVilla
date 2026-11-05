// Delta table schema: parsing (from_delta_schema, table_schema) and
// conforming data files to it (conform_to_schema)
#include <arrow/io/memory.h>
#include <algorithm>
#include <arrow/json/from_string.h>
#include <gtest/gtest.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>

#include "Connectors/MemoryConnector.hpp"
#include "Helpers/DeltaSchema.hpp"
#include "Helpers/Helper.hpp"
#include "LakeVilla/TransactionManager/ManagerDeltaLake.hpp"
#include "test_util.hpp"

namespace {

using arrow::field;

std::shared_ptr<arrow::Array> array(const std::shared_ptr<arrow::DataType>& t,
                                    const std::string& json) {
  return arrow::json::ArrayFromJSONString(t, json).ValueOrDie();
}

// dates and timestamps from their text form (std::nullopt = null)
std::shared_ptr<arrow::Array> parsed(
    const std::shared_ptr<arrow::DataType>& t,
    const std::vector<std::optional<std::string>>& values) {
  std::unique_ptr<arrow::ArrayBuilder> builder;
  EXPECT_TRUE(
      arrow::MakeBuilder(arrow::default_memory_pool(), t, &builder).ok());
  for (auto& value : values) {
    if (value) {
      EXPECT_TRUE(builder
                      ->AppendScalar(
                          *arrow::Scalar::Parse(t, *value).ValueOrDie())
                      .ok());
    } else {
      EXPECT_TRUE(builder->AppendNull().ok());
    }
  }
  return builder->Finish().ValueOrDie();
}

std::shared_ptr<arrow::Table> table(
    const arrow::FieldVector& fields,
    const std::vector<std::shared_ptr<arrow::Array>>& columns) {
  return arrow::Table::Make(arrow::schema(fields), columns);
}

std::shared_ptr<arrow::Schema> parse(const std::string& schema_string) {
  std::string error;
  auto schema = LHHelpers::from_delta_schema(schema_string, &error);
  EXPECT_TRUE(schema.has_value()) << error;
  return schema ? *schema : nullptr;
}

std::string column(const std::string& name, const std::string& type,
                   bool nullable = true, const std::string& metadata = "{}") {
  return R"({"name":")" + name + R"(","type":)" + type +
         R"(,"nullable":)" + (nullable ? "true" : "false") +
         R"(,"metadata":)" + metadata + "}";
}

std::string struct_of(const std::vector<std::string>& columns) {
  std::string out = R"({"type":"struct","fields":[)";
  for (size_t i = 0; i < columns.size(); i++) {
    out += (i ? "," : "") + columns[i];
  }
  return out + "]}";
}

// a Parquet file as Spark writes it (no stored Arrow schema; INT96)
std::string spark_parquet(const std::shared_ptr<arrow::Table>& t) {
  auto sink = arrow::io::BufferOutputStream::Create().ValueOrDie();
  parquet::ArrowWriterProperties::Builder arrow_properties;
  arrow_properties.enable_deprecated_int96_timestamps();
  PARQUET_THROW_NOT_OK(parquet::arrow::WriteTable(
      *t, arrow::default_memory_pool(), sink, 1024,
      parquet::default_writer_properties(), arrow_properties.build()));
  return sink->Finish().ValueOrDie()->ToString();
}

}  // namespace

TEST(Schema, PrimitiveTypes) {
  auto schema = parse(struct_of(
      {column("s", R"("string")"), column("l", R"("long")"),
       column("i", R"("integer")"), column("sh", R"("short")"),
       column("b", R"("byte")"), column("f", R"("float")"),
       column("d", R"("double")"), column("flag", R"("boolean")"),
       column("bin", R"("binary")"), column("day", R"("date")"),
       column("ts", R"("timestamp")"), column("ntz", R"("timestamp_ntz")"),
       column("dec", "\"decimal(10,2)\""), column("c", "\"char(3)\""),
       column("v", "\"varchar(20)\""), column("nothing", R"("void")")}));
  ASSERT_NE(schema, nullptr);
  auto expected = arrow::schema(
      {field("s", arrow::utf8()), field("l", arrow::int64()),
       field("i", arrow::int32()), field("sh", arrow::int16()),
       field("b", arrow::int8()), field("f", arrow::float32()),
       field("d", arrow::float64()), field("flag", arrow::boolean()),
       field("bin", arrow::binary()), field("day", arrow::date32()),
       field("ts", arrow::timestamp(arrow::TimeUnit::MICRO, "UTC")),
       field("ntz", arrow::timestamp(arrow::TimeUnit::MICRO)),
       field("dec", arrow::decimal128(10, 2)), field("c", arrow::utf8()),
       field("v", arrow::utf8()), field("nothing", arrow::null())});
  EXPECT_TRUE(schema->Equals(*expected)) << schema->ToString();
}

TEST(Schema, NestedTypesAndNullability) {
  auto schema = parse(struct_of(
      {column("id", R"("long")", false),
       column("tags",
              R"({"type":"array","elementType":"string","containsNull":false})"),
       column("attrs",
              R"({"type":"map","keyType":"string","valueType":"double","valueContainsNull":true})"),
       column("address",
              struct_of({column("city", R"("string")"),
                         column("zip", R"("integer")", false)}))}));
  ASSERT_NE(schema, nullptr);
  auto expected = arrow::schema(
      {field("id", arrow::int64(), false),
       field("tags", arrow::list(field("element", arrow::utf8(), false))),
       field("attrs",
             arrow::map(arrow::utf8(), field("value", arrow::float64()))),
       field("address",
             arrow::struct_({field("city", arrow::utf8()),
                             field("zip", arrow::int32(), false)}))});
  EXPECT_TRUE(schema->Equals(*expected)) << schema->ToString();
}

TEST(Schema, FieldMetadata) {
  auto schema = parse(struct_of({column(
      "c", R"("string")", true,
      R"({"comment":"the key","delta.columnMapping.id":3,"delta.columnMapping.physicalName":"col-1"})")}));
  ASSERT_NE(schema, nullptr);
  auto metadata = schema->field(0)->metadata();
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->Get("comment").ValueOrDie(), "the key");
  EXPECT_EQ(metadata->Get("delta.columnMapping.id").ValueOrDie(), "3");
  EXPECT_EQ(metadata->Get("delta.columnMapping.physicalName").ValueOrDie(),
            "col-1");
}

TEST(Schema, InvalidSchemas) {
  std::string error;
  EXPECT_FALSE(LHHelpers::from_delta_schema("not json", &error));
  EXPECT_FALSE(error.empty());
  error.clear();
  EXPECT_FALSE(LHHelpers::from_delta_schema(
      struct_of({column("x", R"("interval")")}), &error));
  EXPECT_NE(error.find("interval"), std::string::npos) << error;
  EXPECT_FALSE(LHHelpers::from_delta_schema(
      struct_of({column("x", "\"decimal(50,2)\"")}), &error));
  EXPECT_FALSE(LHHelpers::from_delta_schema(
      struct_of({column("x", R"({"type":"udt"})")}), &error));
}

TEST(Schema, RoundTripWithToDeltaSchema) {
  auto original = arrow::schema(
      {field("id", arrow::utf8(), false), field("n", arrow::int64()),
       field("ts", arrow::timestamp(arrow::TimeUnit::MICRO, "UTC")),
       field("ntz", arrow::timestamp(arrow::TimeUnit::MICRO)),
       field("dec", arrow::decimal128(38, 18)),
       field("list", arrow::list(field("element", arrow::int32()))),
       field("map",
             arrow::map(arrow::utf8(), field("value", arrow::boolean()))),
       field("s", arrow::struct_({field("a", arrow::date32())}))});
  auto delta = LHHelpers::to_delta_schema(*original);
  ASSERT_TRUE(delta.has_value());
  auto parsed = parse(delta->schema_string);
  ASSERT_NE(parsed, nullptr);
  EXPECT_TRUE(parsed->Equals(*original)) << parsed->ToString();
  EXPECT_EQ(LHHelpers::to_delta_schema(*parsed)->schema_string,
            delta->schema_string);
}

TEST(Schema, TableSchemaOfMetadata) {
  std::string schema = struct_of({column("id", R"("string")"),
                                  column("p", R"("date")")});
  rapidjson::Document doc;
  doc.SetObject();
  auto& a = doc.GetAllocator();
  doc.AddMember("schemaString", rapidjson::Value(schema.c_str(), a), a);
  rapidjson::Value partitions(rapidjson::kArrayType);
  partitions.PushBack("p", a);
  doc.AddMember("partitionColumns", partitions, a);
  rapidjson::Value configuration(rapidjson::kObjectType);
  configuration.AddMember("delta.columnMapping.mode", "name", a);
  doc.AddMember("configuration", configuration, a);
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  auto result = LHHelpers::table_schema(buffer.GetString());
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->schema->num_fields(), 2);
  EXPECT_EQ(result->partition_columns, std::vector<std::string>{"p"});
  EXPECT_TRUE(result->column_mapping);
  EXPECT_FALSE(LHHelpers::table_schema(R"({"id":"x"})"));
}

// ---- conform_to_schema ------------------------------------------------------

TEST(Conform, SparkInt96TimestampsAsDeltaTimestamps) {
  auto naive = arrow::timestamp(arrow::TimeUnit::MICRO);
  auto file = table({field("ts", naive)},
                    {parsed(naive, {"1500-01-01 00:00:00", "2024-01-01 12:00:00.123456",
                     "9999-12-31 23:59:59.999999", std::nullopt})});
  auto bytes = spark_parquet(file);
  auto read = LHHelpers::readParquetAsTable(bytes.data(), bytes.size());
  ASSERT_NE(read, nullptr);
  // INT96 is read in microseconds (nanoseconds would overflow before 1677)
  EXPECT_TRUE(read->schema()->field(0)->type()->Equals(*naive))
      << read->schema()->ToString();

  auto delta = arrow::timestamp(arrow::TimeUnit::MICRO, "UTC");
  auto conformed =
      LHHelpers::conform_to_schema(read, *arrow::schema({field("ts", delta)}));
  ASSERT_TRUE(conformed.ok()) << conformed.status().ToString();
  auto expected = parsed(delta, {"1500-01-01 00:00:00", "2024-01-01 12:00:00.123456",
                     "9999-12-31 23:59:59.999999", std::nullopt});
  EXPECT_TRUE((*conformed)->column(0)->chunk(0)->Equals(*expected))
      << (*conformed)->column(0)->ToString();
}

TEST(Conform, NanosecondsAreTruncatedLikeSpark) {
  auto ns = arrow::timestamp(arrow::TimeUnit::NANO);
  auto file = table({field("ts", ns)},
                    {array(ns, "[1704110400123456789, -1, -1000, null]")});
  auto target = arrow::timestamp(arrow::TimeUnit::MICRO, "UTC");
  auto conformed =
      LHHelpers::conform_to_schema(file, *arrow::schema({field("ts", target)}));
  ASSERT_TRUE(conformed.ok()) << conformed.status().ToString();
  EXPECT_TRUE((*conformed)->column(0)->chunk(0)->Equals(
      *array(target, "[1704110400123456, -1, -1, null]")))
      << (*conformed)->column(0)->ToString();
}

TEST(Conform, NamesOrderTypesAndNullability) {
  // the file: other case, other order, narrower types, an extra column,
  // a non-nullable column the schema declares nullable
  auto file = table(
      {field("V", arrow::int32()), field("ID", arrow::utf8(), false),
       field("dropped", arrow::utf8()), field("f", arrow::float32()),
       field("dec", arrow::decimal128(5, 2))},
      {array(arrow::int32(), "[1, 2]"), array(arrow::utf8(), R"(["a", "b"])"),
       array(arrow::utf8(), R"(["x", "y"])"),
       array(arrow::float32(), "[1.5, null]"),
       array(arrow::decimal128(5, 2), R"(["1.25", "-3.10"])")});
  auto schema = arrow::schema(
      {field("id", arrow::utf8()), field("v", arrow::int64()),
       field("f", arrow::float64()), field("dec", arrow::decimal128(10, 3)),
       field("added", arrow::utf8())});
  auto conformed = LHHelpers::conform_to_schema(file, *schema);
  ASSERT_TRUE(conformed.ok()) << conformed.status().ToString();
  auto& t = *conformed;
  EXPECT_TRUE(t->schema()->Equals(*schema)) << t->schema()->ToString();
  EXPECT_TRUE(t->column(0)->chunk(0)->Equals(
      *array(arrow::utf8(), R"(["a", "b"])")));
  EXPECT_TRUE(t->column(1)->chunk(0)->Equals(*array(arrow::int64(), "[1, 2]")));
  EXPECT_TRUE(
      t->column(2)->chunk(0)->Equals(*array(arrow::float64(), "[1.5, null]")));
  EXPECT_TRUE(t->column(3)->chunk(0)->Equals(
      *array(arrow::decimal128(10, 3), R"(["1.250", "-3.100"])")));
  EXPECT_EQ(t->column(4)->null_count(), 2);

  // writes: columns outside the schema are an error
  LHHelpers::ConformOptions write;
  write.reject_extra_columns = true;
  auto rejected = LHHelpers::conform_to_schema(file, *schema, write);
  ASSERT_FALSE(rejected.ok());
  EXPECT_NE(rejected.status().message().find("dropped"), std::string::npos);
}

TEST(Conform, LossyConversionsFail) {
  auto file = table({field("v", arrow::int64())},
                    {array(arrow::int64(), "[5000000000]")});
  auto narrowed = LHHelpers::conform_to_schema(
      file, *arrow::schema({field("v", arrow::int32())}));
  ASSERT_FALSE(narrowed.ok());
  EXPECT_NE(narrowed.status().message().find("column v"), std::string::npos)
      << narrowed.status().ToString();
}

TEST(Conform, NotNullColumns) {
  auto schema = arrow::schema({field("id", arrow::utf8(), false),
                               field("v", arrow::int64())});
  auto with_null = table({field("id", arrow::utf8()), field("v", arrow::int64())},
                         {array(arrow::utf8(), R"(["a", null])"),
                          array(arrow::int64(), "[1, 2]")});
  EXPECT_FALSE(LHHelpers::conform_to_schema(with_null, *schema).ok());
  auto missing = table({field("v", arrow::int64())},
                       {array(arrow::int64(), "[1]")});
  EXPECT_FALSE(LHHelpers::conform_to_schema(missing, *schema).ok());
  // a nullable file column without nulls is fine
  auto ok = table({field("id", arrow::utf8()), field("v", arrow::int64())},
                  {array(arrow::utf8(), R"(["a"])"),
                   array(arrow::int64(), "[1]")});
  auto conformed = LHHelpers::conform_to_schema(ok, *schema);
  ASSERT_TRUE(conformed.ok()) << conformed.status().ToString();
  EXPECT_FALSE((*conformed)->schema()->field(0)->nullable());
}

TEST(Conform, NestedTypes) {
  auto file_struct = arrow::struct_(
      {field("ZIP", arrow::int32()), field("city", arrow::utf8())});
  auto file_list = arrow::list(field("item", arrow::int32()));
  auto file_map = arrow::map(arrow::utf8(), arrow::int32());
  auto file = table(
      {field("address", file_struct), field("scores", file_list),
       field("attrs", file_map)},
      {array(file_struct, R"([{"ZIP": 1, "city": "a"}, null])"),
       array(file_list, "[[1, 2], null]"),
       array(file_map, R"([[["k", 1]], null])")});

  auto target_struct = arrow::struct_(
      {field("city", arrow::utf8()), field("zip", arrow::int64()),
       field("country", arrow::utf8())});
  auto target_list = arrow::list(field("element", arrow::int64()));
  auto target_map = arrow::map(arrow::utf8(), field("value", arrow::int64()));
  auto schema = arrow::schema({field("address", target_struct),
                               field("scores", target_list),
                               field("attrs", target_map)});
  auto conformed = LHHelpers::conform_to_schema(file, *schema);
  ASSERT_TRUE(conformed.ok()) << conformed.status().ToString();
  auto& t = *conformed;
  EXPECT_TRUE(t->schema()->Equals(*schema)) << t->schema()->ToString();
  EXPECT_TRUE(t->column(0)->chunk(0)->Equals(*array(
      target_struct, R"([{"city": "a", "zip": 1, "country": null}, null])")))
      << t->column(0)->ToString();
  EXPECT_TRUE(
      t->column(1)->chunk(0)->Equals(*array(target_list, "[[1, 2], null]")));
  EXPECT_TRUE(t->column(2)->chunk(0)->Equals(
      *array(target_map, R"([[["k", 1]], null])")));
  ASSERT_TRUE(t->ValidateFull().ok()) << t->ValidateFull().ToString();
}

TEST(Conform, SlicedInput) {
  auto file_struct = arrow::struct_({field("a", arrow::int32())});
  auto full = table({field("s", file_struct), field("l",
                                                    arrow::list(arrow::int32()))},
                    {array(file_struct, R"([{"a": 1}, {"a": 2}, {"a": 3}])"),
                     array(arrow::list(arrow::int32()), "[[1], [2], [3]]")});
  auto sliced = full->Slice(1, 2);
  auto target_struct = arrow::struct_({field("a", arrow::int64())});
  auto target_list = arrow::list(field("element", arrow::int64()));
  auto schema = arrow::schema({field("s", target_struct),
                               field("l", target_list)});
  auto conformed = LHHelpers::conform_to_schema(sliced, *schema);
  ASSERT_TRUE(conformed.ok()) << conformed.status().ToString();
  ASSERT_TRUE((*conformed)->ValidateFull().ok());
  auto combined = (*conformed)->CombineChunks().ValueOrDie();
  EXPECT_TRUE(combined->column(0)->chunk(0)->Equals(
      *array(target_struct, R"([{"a": 2}, {"a": 3}])")))
      << combined->column(0)->ToString();
  EXPECT_TRUE(combined->column(1)->chunk(0)->Equals(
      *array(target_list, "[[2], [3]]")));
}

TEST(Conform, PartitionValues) {
  auto file = table({field("id", arrow::utf8())},
                    {array(arrow::utf8(), R"(["a", "b"])")});
  auto schema = arrow::schema(
      {field("id", arrow::utf8()), field("day", arrow::date32()),
       field("n", arrow::int32()), field("ts", arrow::timestamp(
                                                arrow::TimeUnit::MICRO, "UTC")),
       field("none", arrow::utf8())});
  std::vector<std::pair<std::string, std::optional<std::string>>> values = {
      {"DAY", "2024-02-29"},
      {"n", "7"},
      {"ts", "2024-01-01 10:00:00.5"},
      {"none", std::nullopt}};
  LHHelpers::ConformOptions options;
  options.partition_values = &values;
  auto conformed = LHHelpers::conform_to_schema(file, *schema, options);
  ASSERT_TRUE(conformed.ok()) << conformed.status().ToString();
  auto t = (*conformed)->CombineChunks().ValueOrDie();
  EXPECT_TRUE(t->column(1)->chunk(0)->Equals(
      *parsed(arrow::date32(), {"2024-02-29", "2024-02-29"})));
  EXPECT_TRUE(
      t->column(2)->chunk(0)->Equals(*array(arrow::int32(), "[7, 7]")));
  EXPECT_TRUE(t->column(3)->chunk(0)->Equals(
      *parsed(arrow::timestamp(arrow::TimeUnit::MICRO, "UTC"),
             {"2024-01-01 10:00:00.5", "2024-01-01 10:00:00.5"})));
  EXPECT_EQ(t->column(4)->null_count(), 2);

  values = {{"day", "not a date"}};
  EXPECT_FALSE(LHHelpers::conform_to_schema(file, *schema, options).ok());
}

TEST(Conform, PhysicalNames) {
  auto file = table({field("col-1", arrow::utf8()), field("col-2",
                                                          arrow::int64())},
                    {array(arrow::utf8(), R"(["a"])"),
                     array(arrow::int64(), "[1]")});
  auto named = [](const std::string& name, const std::string& physical,
                  std::shared_ptr<arrow::DataType> type) {
    return field(name, type, true,
                 arrow::key_value_metadata(
                     {"delta.columnMapping.physicalName"}, {physical}));
  };
  auto schema = arrow::schema({named("id", "col-1", arrow::utf8()),
                               named("renamed", "col-2", arrow::int64())});
  LHHelpers::ConformOptions options;
  options.physical_names = true;
  auto conformed = LHHelpers::conform_to_schema(file, *schema, options);
  ASSERT_TRUE(conformed.ok()) << conformed.status().ToString();
  EXPECT_EQ((*conformed)->schema()->field(1)->name(), "renamed");
  EXPECT_TRUE((*conformed)->column(1)->chunk(0)->Equals(
      *array(arrow::int64(), "[1]")));
  // without column mapping the logical names are used
  auto without = LHHelpers::conform_to_schema(file, *schema);
  ASSERT_TRUE(without.ok());
  EXPECT_EQ((*without)->column(0)->null_count(), 1);
}

TEST(Conform, MatchingColumnsAreNotCopied) {
  auto values = array(arrow::utf8(), R"(["a", "b"])");
  auto file = table({field("id", arrow::utf8())}, {values});
  auto conformed = LHHelpers::conform_to_schema(
      file, *arrow::schema({field("id", arrow::utf8())}));
  ASSERT_TRUE(conformed.ok());
  EXPECT_EQ((*conformed)->column(0)->chunk(0)->data()->buffers[2],
            values->data()->buffers[2]);
}

TEST(Conform, DataFilesAreWrittenInMicroseconds) {
  auto ns = arrow::timestamp(arrow::TimeUnit::NANO, "UTC");
  auto t = table({field("ts", ns)}, {array(ns, "[1704110400123456789]")});
  auto bytes = LHHelpers::encodeAsParquet_str(t);
  auto buffer = std::make_shared<arrow::Buffer>(
      reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
  auto reader = parquet::ParquetFileReader::Open(
      std::make_shared<arrow::io::BufferReader>(buffer));
  auto logical = reader->metadata()->schema()->Column(0)->logical_type();
  EXPECT_NE(logical->ToString().find("timeUnit=microseconds"), std::string::npos)
      << logical->ToString();
}

// Spark reads old dates only from files that name Spark >= 3.0 (proleptic
// Gregorian calendar) in their key-value metadata
TEST(Conform, DataFilesDeclareTheProlepticCalendar) {
  auto with_metadata = table({field("d", arrow::date32())},
                             {parsed(arrow::date32(), {"1500-01-01"})})
                           ->ReplaceSchemaMetadata(
                               arrow::key_value_metadata({"mine"}, {"kept"}));
  auto bytes = LHHelpers::encodeAsParquet_str(with_metadata);
  auto buffer = std::make_shared<arrow::Buffer>(
      reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
  auto reader = parquet::ParquetFileReader::Open(
      std::make_shared<arrow::io::BufferReader>(buffer));
  auto metadata = reader->metadata()->key_value_metadata();
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->Get("org.apache.spark.version").ValueOr(""), "3.0.0");
  EXPECT_FALSE(metadata->Contains("org.apache.spark.legacyDateTime"));
  EXPECT_EQ(metadata->Get("mine").ValueOr(""), "kept");

  // the values are unchanged
  auto read = LHHelpers::readParquetAsTable(bytes.data(), bytes.size());
  ASSERT_NE(read, nullptr);
  EXPECT_TRUE(read->column(0)->chunk(0)->Equals(
      *parsed(arrow::date32(), {"1500-01-01"})));
}

// ---- transactions on tables with Spark and LakeVilla files -----------------

namespace {

using LHTransactions::TransactionManagerDeltaLake;
using TableResult = TransactionManagerDeltaLake::TableResult;

const auto NAIVE = arrow::timestamp(arrow::TimeUnit::MICRO);
const auto UTC = arrow::timestamp(arrow::TimeUnit::MICRO, "UTC");

// add action of a data file, with statistics on the key column "id"
std::string add_line(const std::string& file, size_t size,
                     const std::string& key,
                     const std::string& partition_values = "{}") {
  return R"({"add":{"path":")" + file + R"(","partitionValues":)" +
         partition_values + R"(,"size":)" + std::to_string(size) +
         R"(,"modificationTime":1700000000000,"dataChange":true,"stats":"{\"numRecords\":1,\"minValues\":{\"id\":\")" +
         key + R"(\"},\"maxValues\":{\"id\":\")" + key +
         R"(\"},\"nullCount\":{\"id\":0}}"}})";
}

class SchemaTransactions : public ::testing::Test {
 protected:
  StorageConnector::MemoryConnector conn;
  std::string base = "wh/schema.db/t/";

  std::unique_ptr<TransactionManagerDeltaLake> manager(uint32_t id) {
    return std::make_unique<TransactionManagerDeltaLake>(
        LHTransactions::LakeVillaConf{true, true, false}, base, &conn, id);
  }

  // (id STRING NOT NULL, ts TIMESTAMP, v BIGINT, note STRING)
  static std::shared_ptr<arrow::Schema> schema() {
    return arrow::schema({field("id", arrow::utf8(), false), field("ts", UTC),
                          field("v", arrow::int64()),
                          field("note", arrow::utf8())});
  }

  void create() {
    auto tm = manager(1);
    ASSERT_EQ(tm->create_table(base, *schema()), TableResult::Ok);
  }

  // a data file as Spark writes it (INT96, nullable columns) in a version
  void spark_file(const std::string& key, const std::string& ts, int64_t v,
                  uint64_t version) {
    auto rows = table(
        {field("id", arrow::utf8()), field("ts", NAIVE),
         field("v", arrow::int64()), field("note", arrow::utf8())},
        {array(arrow::utf8(), "[\"" + key + "\"]"), parsed(NAIVE, {ts}),
         array(arrow::int64(), "[" + std::to_string(v) + "]"),
         array(arrow::utf8(), "[null]")});
    auto bytes = spark_parquet(rows);
    auto name = "part-" + key + ".snappy.parquet";
    conn.write(base + name, bytes.data(), bytes.size());
    lvtest::write_version(conn, base, version,
                          {add_line(name, bytes.size(), key)});
  }

  std::shared_ptr<arrow::Table> read_all() {
    auto tm = manager(99);
    EXPECT_TRUE(tm->begin_transaction_ycsb());
    auto rows = tm->read_table(0);
    tm->commit(true);
    return rows ? rows->CombineChunks().ValueOrDie() : nullptr;
  }

  // the value of a column in the row of a key
  static std::string value(const std::shared_ptr<arrow::Table>& rows,
                           const std::string& key, const std::string& column) {
    auto ids = std::static_pointer_cast<arrow::StringArray>(
        rows->GetColumnByName("id")->chunk(0));
    for (int64_t i = 0; i < ids->length(); i++) {
      if (ids->GetString(i) == key) {
        auto scalar = rows->GetColumnByName(column)->GetScalar(i).ValueOrDie();
        return scalar->ToString();
      }
    }
    return "<missing>";
  }

  // Arrow schemas of the data files LakeVilla wrote (lhdata-*)
  std::vector<std::shared_ptr<arrow::Schema>> written_schemas() {
    std::vector<std::shared_ptr<arrow::Schema>> schemas;
    auto objects = conn.list_objects(base);
    for (auto& object : *objects) {
      if (object.key.find("/lhdata-") == std::string::npos) {
        continue;
      }
      auto bytes = *conn.get(object.key);
      schemas.push_back(
          LHHelpers::readParquetAsTable(bytes.data(), bytes.size())->schema());
    }
    return schemas;
  }
};

}  // namespace

TEST_F(SchemaTransactions, SparkAndLakeVillaFilesInOneTable) {
  create();
  spark_file("s1", "1500-01-01 12:00:00.123456", 1, 1);

  auto tm = manager(2);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  // other case, int32 instead of BIGINT, no note column
  auto rows = table({field("ID", arrow::utf8(), false), field("ts", UTC),
                     field("v", arrow::int32())},
                    {array(arrow::utf8(), R"(["l1"])"),
                     parsed(UTC, {"1999-12-31 23:59:59.000001"}),
                     array(arrow::int32(), "[2]")});
  ASSERT_TRUE(tm->add_file(0, rows, "l1"));
  ASSERT_TRUE(tm->commit(false));

  auto all = read_all();
  ASSERT_NE(all, nullptr);
  EXPECT_TRUE(all->schema()->Equals(*schema())) << all->schema()->ToString();
  EXPECT_EQ(all->num_rows(), 2);
  EXPECT_EQ(value(all, "s1", "ts"), "1500-01-01 12:00:00.123456Z");
  EXPECT_EQ(value(all, "l1", "ts"), "1999-12-31 23:59:59.000001Z");
  EXPECT_EQ(value(all, "l1", "v"), "2");
  EXPECT_EQ(value(all, "l1", "note"), "null");

  // single files are conformed as well
  auto reader = manager(3);
  ASSERT_TRUE(reader->begin_transaction_ycsb());
  std::string path;
  auto file = reader->read_file_as_table(0, "s1", path);
  ASSERT_NE(file, nullptr);
  EXPECT_TRUE(file->schema()->Equals(*schema())) << file->schema()->ToString();
  reader->commit(true);

  // LakeVilla's file has the table's names and types
  auto written = written_schemas();
  ASSERT_EQ(written.size(), 1u);
  EXPECT_TRUE(written[0]->Equals(*schema())) << written[0]->ToString();
}

TEST_F(SchemaTransactions, EmptyTableHasTheSchema) {
  create();
  auto all = read_all();
  ASSERT_NE(all, nullptr);
  EXPECT_EQ(all->num_rows(), 0);
  EXPECT_TRUE(all->schema()->Equals(*schema()));
}

TEST_F(SchemaTransactions, RowsThatDoNotFitAreRefused) {
  create();
  auto tm = manager(2);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  auto extra = table({field("id", arrow::utf8()), field("other", arrow::utf8())},
                     {array(arrow::utf8(), R"(["a"])"),
                      array(arrow::utf8(), R"(["x"])")});
  EXPECT_FALSE(tm->add_file(0, extra, "a"));
  auto null_key = table({field("id", arrow::utf8())},
                        {array(arrow::utf8(), "[null]")});
  EXPECT_FALSE(tm->add_file(0, null_key, "a"));
  auto text_number = table({field("id", arrow::utf8()),
                            field("v", arrow::utf8())},
                           {array(arrow::utf8(), R"(["a"])"),
                            array(arrow::utf8(), R"(["no number"])")});
  EXPECT_FALSE(tm->add_file(0, text_number, "a"));
  tm->abort();
  EXPECT_TRUE(written_schemas().empty());
}

TEST_F(SchemaTransactions, PartitionedTables) {
  std::string metadata =
      R"({"metaData":{"id":"t","format":{"provider":"parquet","options":{}},"schemaString":"{\"type\":\"struct\",\"fields\":[{\"name\":\"id\",\"type\":\"string\",\"nullable\":true,\"metadata\":{}},{\"name\":\"p\",\"type\":\"date\",\"nullable\":true,\"metadata\":{}}]}","partitionColumns":["p"],"configuration":{},"createdTime":1}})";
  auto rows = table({field("id", arrow::utf8())},
                    {array(arrow::utf8(), R"(["k1"])")});
  auto bytes = spark_parquet(rows);
  conn.write(base + "p=2024-02-29/part-0.parquet", bytes.data(), bytes.size());
  lvtest::write_version(
      conn, base, 0,
      {R"({"protocol":{"minReaderVersion":1,"minWriterVersion":2}})",
       metadata,
       add_line("p=2024-02-29/part-0.parquet", bytes.size(), "k1",
                R"({"p":"2024-02-29"})")});

  auto all = read_all();
  ASSERT_NE(all, nullptr);
  EXPECT_EQ(all->num_columns(), 2);
  EXPECT_EQ(value(all, "k1", "p"), "2024-02-29");

  // LakeVilla does not write partitionValues: writes are refused
  auto tm = manager(2);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  EXPECT_FALSE(tm->add_file(0, rows, "k2"));
  std::string encoded = bytes;
  EXPECT_FALSE(tm->add_file(0, encoded, std::string("k2")));
  tm->abort();
}

TEST_F(SchemaTransactions, RedoOfASparkFileKeepsTheSchema) {
  create();
  spark_file("s1", "2024-01-01 12:00:00.123456", 1, 1);

  // t2 starts while t1 holds a marker: its update is logged implicitly and
  // redone from the Spark file after t1's commit
  auto t1 = manager(10);
  auto t2 = manager(11);
  ASSERT_TRUE(t1->begin_transaction_ycsb());
  ASSERT_TRUE(t2->begin_transaction_ycsb());

  std::string path;
  auto old = t2->read_file_as_table(0, "s1", path);
  ASSERT_NE(old, nullptr);
  std::vector<std::pair<std::string, std::string>> values = {{"NOTE", "t2"}};
  auto prev = t2->register_update_pairs(
      values, LHTransactions::UpdateOperations::REP, values);
  ASSERT_TRUE(t2->remove_file(0, std::string("s1"), &path, prev));
  ASSERT_TRUE(t2->add_file(0, old, "s1", prev));

  auto row = table({field("id", arrow::utf8())},
                   {array(arrow::utf8(), R"(["l1"])")});
  ASSERT_TRUE(t1->add_file(0, row, "l1"));
  ASSERT_TRUE(t1->commit(false));
  ASSERT_TRUE(t2->commit(false));
  EXPECT_GT(t2->times_redo, 0u);

  auto all = read_all();
  ASSERT_NE(all, nullptr);
  EXPECT_EQ(all->num_rows(), 2);
  EXPECT_EQ(value(all, "s1", "note"), "t2");
  EXPECT_EQ(value(all, "s1", "ts"), "2024-01-01 12:00:00.123456Z");
  for (auto& written : written_schemas()) {
    EXPECT_TRUE(written->Equals(*schema())) << written->ToString();
  }
}

// ---- key-based operations on files with several rows ------------------------

namespace {

// add action with the given statistics JSON ("" = no statistics)
std::string add_with_stats(const std::string& file, size_t size,
                           const std::string& stats) {
  std::string escaped;
  for (char c : stats) {
    escaped += c == '"' ? std::string("\\\"") : std::string(1, c);
  }
  return R"({"add":{"path":")" + file +
         R"(","partitionValues":{},"size":)" + std::to_string(size) +
         R"(,"modificationTime":1700000000000,"dataChange":true)" +
         (stats.empty() ? "" : R"(,"stats":")" + escaped + "\"") + "}}";
}

// statistics of the key column id
std::string id_stats(const std::string& min, const std::string& max,
                     int64_t records, bool numeric = false) {
  auto value = [numeric](const std::string& v) {
    return numeric ? v : "\"" + v + "\"";
  };
  return "{\"numRecords\":" + std::to_string(records) +
         ",\"minValues\":{\"id\":" + value(min) + "},\"maxValues\":{\"id\":" +
         value(max) + "},\"nullCount\":{\"id\":0}}";
}

class KeyRows : public SchemaTransactions {
 protected:
  // a Spark file with the rows (id, null, row number, null)
  void file(const std::string& name, const std::vector<std::string>& ids,
            uint64_t version, const std::string& stats) {
    std::string id_json = "[", v_json = "[";
    std::vector<std::optional<std::string>> no_ts;
    for (size_t i = 0; i < ids.size(); i++) {
      id_json += (i ? ",\"" : "\"") + ids[i] + "\"";
      v_json += (i ? "," : "") + std::to_string(i);
      no_ts.push_back(std::nullopt);
    }
    auto rows = table(
        {field("id", arrow::utf8()), field("ts", NAIVE),
         field("v", arrow::int64()), field("note", arrow::utf8())},
        {array(arrow::utf8(), id_json + "]"), parsed(NAIVE, no_ts),
         array(arrow::int64(), v_json + "]"),
         array(arrow::utf8(), std::string("[") +
                                  (ids.size() > 1 ? "null," : "") +
                                  std::string(ids.size() > 2 ? "null," : "") +
                                  "null]")});
    write_file(name, rows, version, stats);
  }

  void write_file(const std::string& name,
                  const std::shared_ptr<arrow::Table>& rows, uint64_t version,
                  const std::string& stats) {
    auto bytes = spark_parquet(rows);
    conn.write(base + name, bytes.data(), bytes.size());
    lvtest::write_version(conn, base, version,
                          {add_with_stats(name, bytes.size(), stats)});
  }

  static std::vector<std::string> ids(
      const std::shared_ptr<arrow::Table>& rows) {
    std::vector<std::string> result;
    if (!rows) {
      return result;
    }
    auto column = rows->GetColumnByName("id");
    for (int64_t i = 0; i < rows->num_rows(); i++) {
      result.push_back(column->GetScalar(i).ValueOrDie()->ToString());
    }
    std::sort(result.begin(), result.end());
    return result;
  }

  // the rows with a column replaced by a constant string
  static std::shared_ptr<arrow::Table> with_note(
      const std::shared_ptr<arrow::Table>& rows, const std::string& note) {
    std::string json = "[";
    for (int64_t i = 0; i < rows->num_rows(); i++) {
      json += (i ? ",\"" : "\"") + note + "\"";
    }
    auto index = rows->schema()->GetFieldIndex("note");
    return rows
        ->SetColumn(index, rows->schema()->field(index),
                    std::make_shared<arrow::ChunkedArray>(
                        array(arrow::utf8(), json + "]")))
        .ValueOrDie();
  }
};

}  // namespace

TEST_F(KeyRows, KeyInsideAFile) {
  create();
  file("f1.parquet", {"c1", "c2", "c3"}, 1, id_stats("c1", "c3", 3));

  auto tm = manager(2);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  std::string path;
  auto rows = tm->read_file_as_table(0, "c2", path);
  ASSERT_NE(rows, nullptr);
  EXPECT_EQ(ids(rows), std::vector<std::string>{"c2"});
  EXPECT_EQ(path, base + "f1.parquet");

  // an update as the drivers do it: remove the key, add the new row
  std::vector<std::pair<std::string, std::string>> values = {{"note", "new"}};
  auto prev = tm->register_update_pairs(
      values, LHTransactions::UpdateOperations::REP, values);
  ASSERT_TRUE(tm->remove_file(0, std::string("c2"), &path, prev));
  ASSERT_TRUE(tm->add_file(0, with_note(rows, "new"), "c2", prev));
  ASSERT_TRUE(tm->commit(false));

  auto all = read_all();
  ASSERT_NE(all, nullptr);
  EXPECT_EQ(ids(all), (std::vector<std::string>{"c1", "c2", "c3"}));
  EXPECT_EQ(value(all, "c2", "note"), "new");
  EXPECT_EQ(value(all, "c1", "note"), "null");
  EXPECT_EQ(value(all, "c3", "v"), "2");
}

TEST_F(KeyRows, RemoveKeepsTheOtherRowsOfTheFile) {
  create();
  file("f1.parquet", {"c1", "c2", "c3"}, 1, id_stats("c1", "c3", 3));
  auto tm = manager(2);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  ASSERT_TRUE(tm->remove_file(0, std::string("c3")));
  ASSERT_TRUE(tm->commit(false));

  auto all = read_all();
  EXPECT_EQ(ids(all), (std::vector<std::string>{"c1", "c2"}));
  // the remaining rows are in a new file of LakeVilla
  EXPECT_EQ(written_schemas().size(), 1u);
}

TEST_F(KeyRows, KeyInSeveralFiles) {
  create();
  file("f1.parquet", {"a", "b"}, 1, id_stats("a", "b", 2));
  file("f2.parquet", {"b"}, 2, id_stats("b", "b", 1));
  auto tm = manager(2);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  std::string path;
  EXPECT_EQ(ids(tm->read_file_as_table(0, "b", path)),
            (std::vector<std::string>{"b", "b"}));
  ASSERT_TRUE(tm->remove_file(0, std::string("b")));
  ASSERT_TRUE(tm->commit(false));
  EXPECT_EQ(ids(read_all()), std::vector<std::string>{"a"});
}

TEST_F(KeyRows, FilesWithoutStatisticsAreRead) {
  create();
  file("f1.parquet", {"x", "y"}, 1, "");
  auto tm = manager(2);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  std::string path;
  EXPECT_EQ(ids(tm->read_file_as_table(0, "y", path)),
            std::vector<std::string>{"y"});
  EXPECT_EQ(tm->read_file_as_table(0, "z", path), nullptr);
  EXPECT_EQ(path, "");
  tm->commit(true);
}

TEST_F(KeyRows, LongKeysWithTruncatedStatistics) {
  create();
  // Delta truncates string statistics to 32 characters
  std::string a(40, 'a'), b(40, 'b');
  file("fa.parquet", {a}, 1, id_stats(a.substr(0, 32), a.substr(0, 32), 1));
  file("fb.parquet", {b}, 2,
       id_stats(b.substr(0, 32), b.substr(0, 32) + "\xEF\xBF\xBF", 1));
  auto tm = manager(2);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  std::string path;
  EXPECT_EQ(ids(tm->read_file_as_table(0, a, path)),
            std::vector<std::string>{a});
  EXPECT_EQ(ids(tm->read_file_as_table(0, b, path)),
            std::vector<std::string>{b});
  EXPECT_EQ(tm->read_file_as_table(0, std::string(40, 'c'), path), nullptr);
  tm->commit(true);
}

TEST_F(KeyRows, NumericKeysCompareAsNumbers) {
  auto numeric = arrow::schema({field("id", arrow::int64(), false),
                                field("note", arrow::utf8())});
  {
    auto tm = manager(1);
    ASSERT_EQ(tm->create_table(base, *numeric), TableResult::Ok);
  }
  auto rows = [](const std::string& json) {
    return table({field("id", arrow::int64()), field("note", arrow::utf8())},
                 {array(arrow::int64(), json),
                  arrow::MakeArrayOfNull(arrow::utf8(),
                                         arrow::json::ArrayFromJSONString(
                                             arrow::int64(), json)
                                             .ValueOrDie()
                                             ->length())
                      .ValueOrDie()});
  };
  write_file("f1.parquet", rows("[9]"), 1, id_stats("9", "9", 1, true));
  // as text "10" < "5": only a numeric comparison finds 10
  write_file("f2.parquet", rows("[5, 10, 20]"), 2,
             id_stats("5", "20", 3, true));
  auto tm = manager(2);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  std::string path;
  EXPECT_EQ(ids(tm->read_file_as_table(0, "10", path)),
            std::vector<std::string>{"10"});
  EXPECT_EQ(ids(tm->read_file_as_table(0, "9", path)),
            std::vector<std::string>{"9"});
  EXPECT_EQ(tm->read_file_as_table(0, "7", path), nullptr);
  EXPECT_EQ(tm->read_file_as_table(0, "not a number", path), nullptr);
  tm->commit(true);
}

TEST_F(KeyRows, SingleRowFilesAreNotReadToFindAKey) {
  create();
  {
    auto tm = manager(2);
    ASSERT_TRUE(tm->begin_transaction_ycsb());
    for (std::string key : {"k1", "k2", "k3"}) {
      ASSERT_TRUE(tm->add_file(
          0, table({field("id", arrow::utf8())},
                   {array(arrow::utf8(), "[\"" + key + "\"]")}),
          key));
    }
    ASSERT_TRUE(tm->commit(false));
  }
  int data_reads = 0;
  conn.fail_hook = [&data_reads](StorageConnector::MemoryConnector::Op op,
                                 const std::string& path) {
    if (op == StorageConnector::MemoryConnector::Op::Read &&
        path.find("/lhdata-") != std::string::npos) {
      data_reads++;
    }
    return false;
  };
  auto tm = manager(3);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  // the statistics (one row, min = max = key) identify the file
  ASSERT_TRUE(tm->remove_file(0, std::string("k2")));
  EXPECT_EQ(data_reads, 0);
  std::string path;
  EXPECT_EQ(ids(tm->read_file_as_table(0, "k1", path)),
            std::vector<std::string>{"k1"});
  EXPECT_EQ(data_reads, 1);
  ASSERT_TRUE(tm->commit(false));
  conn.fail_hook = nullptr;
  EXPECT_EQ(ids(read_all()), (std::vector<std::string>{"k1", "k3"}));
}

TEST_F(KeyRows, AnUpdateDoesNotRemoveItsNewRow) {
  create();
  file("f1.parquet", {"c1", "c2"}, 1, id_stats("c1", "c2", 2));
  auto tm = manager(2);
  ASSERT_TRUE(tm->begin_transaction_ycsb());
  std::string path;
  auto old = tm->read_file_as_table(0, "c1", path);
  ASSERT_NE(old, nullptr);
  std::vector<std::pair<std::string, std::string>> values = {{"note", "new"}};
  auto prev = tm->register_update_pairs(
      values, LHTransactions::UpdateOperations::REP, values);
  // the drivers run the remove concurrently: here the new row comes first
  ASSERT_TRUE(tm->add_file(0, with_note(old, "new"), "c1", prev));
  ASSERT_TRUE(tm->remove_file(0, std::string("c1"), &path, prev));
  ASSERT_TRUE(tm->commit(false));
  auto all = read_all();
  EXPECT_EQ(ids(all), (std::vector<std::string>{"c1", "c2"}));
  EXPECT_EQ(value(all, "c1", "note"), "new");
}

TEST_F(KeyRows, RedoOnAFileWithSeveralRows) {
  create();
  file("f1.parquet", {"c1", "c2", "c3"}, 1, id_stats("c1", "c3", 3));

  // t2 starts while t1 holds a marker: its update is logged implicitly and
  // redone at commit
  auto t1 = manager(10);
  auto t2 = manager(11);
  ASSERT_TRUE(t1->begin_transaction_ycsb());
  ASSERT_TRUE(t2->begin_transaction_ycsb());

  std::string path;
  auto old = t2->read_file_as_table(0, "c2", path);
  ASSERT_NE(old, nullptr);
  std::vector<std::pair<std::string, std::string>> values = {{"note", "t2"}};
  auto prev = t2->register_update_pairs(
      values, LHTransactions::UpdateOperations::REP, values);
  ASSERT_TRUE(t2->remove_file(0, std::string("c2"), &path, prev));
  ASSERT_TRUE(t2->add_file(0, with_note(old, "t2"), "c2", prev));

  ASSERT_TRUE(t1->add_file(0,
                           table({field("id", arrow::utf8())},
                                 {array(arrow::utf8(), R"(["l1"])")}),
                           "l1"));
  ASSERT_TRUE(t1->commit(false));
  ASSERT_TRUE(t2->commit(false));
  EXPECT_GT(t2->times_redo, 0u);

  auto all = read_all();
  EXPECT_EQ(ids(all), (std::vector<std::string>{"c1", "c2", "c3", "l1"}));
  EXPECT_EQ(value(all, "c2", "note"), "t2");
  EXPECT_EQ(value(all, "c1", "note"), "null");
  EXPECT_EQ(value(all, "c3", "v"), "2");
}
