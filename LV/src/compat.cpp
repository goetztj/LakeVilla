// compat-lv: LakeVilla side of the Spark/Delta Lake compatibility harness
// (compat/run_compat.py). Every invocation runs one LakeVilla operation
// against an S3 bucket and prints JSON lines on stdout; diagnostics of the
// library go to stderr.
//
//   compat-lv [options] create <table> <schema-json> [comment]
//   compat-lv [options] drop <table>
//   compat-lv [options] read <table>
//   compat-lv [options] checkpoint <table>
//   compat-lv [options] txn <ops-json>
//
// <table> is a prefix in the bucket ending with '/'. <schema-json> is a list
// of Delta columns: [{"name":"id","type":"string","nullable":true}, ...].
// <ops-json> is a list of operations of one transaction:
//   {"op":"read","table":T}
//   {"op":"insert","table":T,"rows":[{...}],"file_per_row":true}
//   {"op":"update","table":T,"key":K,"set":{...}}   read, remove_file(key), add_file
//   {"op":"delete","table":T,"key":K}               remove_file(key) (rows of the key)
//   {"op":"remove-file","table":T,"key":K}          remove_file(key)
//   {"op":"lookup","table":T,"key":K}               read_file_as_table(key)
// The table of the first operation is opened by begin, the others with
// open_new_table. Keys are values of the first column (LakeVilla finds the
// rows of a key through the column's min/max statistics and the files).
//
// Options: --levels none|0|1|2|01|02|12|012, --isolation serializable|snapshot,
// --lease-ms N, --grace-ms N, --max-redos N, --repair, --repair-after N,
// --check-at-begin, --checkpoints 0|1, --checkpoint-interval N,
// --global-log <prefix>, --txn-id N, --pause (print
// {"event":"paused"} before the commit and wait for "commit", "abort" or
// "crash" on stdin), --crash (exit before the commit, leaving markers behind).
//
// Values: dates "YYYY-MM-DD", timestamps "YYYY-MM-DD HH:MM:SS.ffffff" (UTC),
// decimals as strings. Environment: LV_S3_ENDPOINT [minio:9000],
// LV_S3_USER [admin], LV_S3_PASSWD [password], LV_S3_REGION [us-east-1],
// LV_S3_BUCKET [compat].
//
// Exit codes: 0 committed / ok, 3 aborted, 1 error.
#include <arrow/api.h>
#include <aws/core/Aws.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Connectors/MinIOConnector.hpp"
#include "Helpers/Helper.hpp"
#include "LakeVilla/TransactionManager/ManagerDeltaLake.hpp"

using Manager = LHTransactions::TransactionManagerDeltaLake;
using Json = rapidjson::Document;

namespace {

struct Options {
  LHTransactions::LakeVillaConf levels = {false, false, false};
  LHTransactions::ProtocolOptions protocol;
  std::string global_log = "_global/";
  uint32_t txn_id = 0;
  bool pause = false;
  bool crash = false;
};

std::string env(const char* name, const char* fallback) {
  auto value = std::getenv(name);
  return value ? value : fallback;
}

std::string lower(std::string value) {
  for (auto& c : value) {
    c = std::tolower(static_cast<unsigned char>(c));
  }
  return value;
}

// ---- output ---------------------------------------------------------------

void print(const rapidjson::Value& value) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  value.Accept(writer);
  std::cout << buffer.GetString() << std::endl;
}

void print_result(const std::string& status, const std::string& error = "",
                  rapidjson::Value* extra = nullptr,
                  Json::AllocatorType* alloc = nullptr) {
  Json d;
  d.SetObject();
  auto& a = d.GetAllocator();
  d.AddMember("event", "result", a);
  d.AddMember("status", rapidjson::Value(status.c_str(), a), a);
  if (!error.empty()) {
    d.AddMember("error", rapidjson::Value(error.c_str(), a), a);
  }
  if (extra && extra->IsObject()) {
    for (auto it = extra->MemberBegin(); it != extra->MemberEnd(); ++it) {
      d.AddMember(rapidjson::Value(it->name, a), rapidjson::Value(it->value, a),
                  a);
    }
  }
  print(d);
}

// ---- dates and timestamps (proleptic Gregorian calendar, UTC) -------------

int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civil_from_days(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = static_cast<int64_t>(yoe) + era * 400 + (m <= 2);
}

std::optional<int32_t> parse_date(const std::string& text) {
  int y;
  unsigned m, d;
  if (std::sscanf(text.c_str(), "%d-%u-%u", &y, &m, &d) != 3) {
    return std::nullopt;
  }
  return static_cast<int32_t>(days_from_civil(y, m, d));
}

// "YYYY-MM-DD[ T]HH:MM:SS[.ffffff]" -> microseconds since the epoch
std::optional<int64_t> parse_timestamp(const std::string& text) {
  int y;
  unsigned mo, d, h, mi, s;
  char sep;
  int consumed = 0;
  if (std::sscanf(text.c_str(), "%d-%u-%u%c%u:%u:%u%n", &y, &mo, &d, &sep, &h,
                  &mi, &s, &consumed) != 7) {
    return std::nullopt;
  }
  int64_t micros = 0;
  if (text[consumed] == '.') {
    int digits = 0;
    for (size_t i = consumed + 1; i < text.size() && std::isdigit(text[i]);
         i++) {
      if (digits < 6) {
        micros = micros * 10 + (text[i] - '0');
        digits++;
      }
    }
    for (; digits < 6; digits++) {
      micros *= 10;
    }
  }
  return days_from_civil(y, mo, d) * 86400000000LL +
         (h * 3600LL + mi * 60LL + s) * 1000000LL + micros;
}

std::string format_date(int64_t days) {
  int64_t y;
  unsigned m, d;
  civil_from_days(days, y, m, d);
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04lld-%02u-%02u",
                static_cast<long long>(y), m, d);
  return buffer;
}

std::string format_timestamp(int64_t micros) {
  int64_t days = micros / 86400000000LL;
  int64_t rest = micros % 86400000000LL;
  if (rest < 0) {
    rest += 86400000000LL;
    days--;
  }
  int64_t secs = rest / 1000000;
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%s %02lld:%02lld:%02lld.%06lld",
                format_date(days).c_str(), static_cast<long long>(secs / 3600),
                static_cast<long long>(secs / 60 % 60),
                static_cast<long long>(secs % 60),
                static_cast<long long>(rest % 1000000));
  return buffer;
}

// ---- schemas --------------------------------------------------------------

// Arrow schema of the schema argument of create, a list of Delta fields
// ({"name", "type", "nullable", "comment"}), parsed by the library
std::shared_ptr<arrow::Schema> create_schema(const std::string& fields_json,
                                             std::string& error) {
  auto fields = LHHelpers::readJSON_rapid(fields_json.c_str());
  if (!fields->IsArray()) {
    error = "schema is not a list of fields";
    return nullptr;
  }
  Json schema;
  schema.SetObject();
  auto& a = schema.GetAllocator();
  schema.AddMember("type", "struct", a);
  rapidjson::Value list(rapidjson::kArrayType);
  for (auto& f : fields->GetArray()) {
    rapidjson::Value field(f, a);
    rapidjson::Value metadata(rapidjson::kObjectType);
    if (field.IsObject() && field.HasMember("comment")) {
      metadata.AddMember("comment", rapidjson::Value(field["comment"], a), a);
      field.RemoveMember("comment");
    }
    field.AddMember("metadata", metadata, a);
    list.PushBack(field, a);
  }
  schema.AddMember("fields", list, a);
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  schema.Accept(writer);
  auto parsed = LHHelpers::from_delta_schema(buffer.GetString(), &error);
  return parsed ? *parsed : nullptr;
}

// schema of an opened table (metaData, parsed by the library)
std::shared_ptr<arrow::Schema> table_schema(Manager& tm, uint32_t id,
                                            std::string& error) {
  auto parsed = LHHelpers::table_schema(
      tm.tables[id]->delta_log->get_metadata(), &error);
  return parsed ? parsed->schema : nullptr;
}

// ---- JSON rows <-> Arrow --------------------------------------------------

const rapidjson::Value* find_member(const rapidjson::Value& row,
                                    const std::string& name) {
  auto it = row.FindMember(name.c_str());
  if (it != row.MemberEnd()) {
    return &it->value;
  }
  // Delta column names are case-insensitive
  for (auto m = row.MemberBegin(); m != row.MemberEnd(); ++m) {
    if (lower(m->name.GetString()) == lower(name)) {
      return &m->value;
    }
  }
  return nullptr;
}

arrow::Status append_value(arrow::ArrayBuilder& builder,
                           const arrow::DataType& type,
                           const rapidjson::Value& v) {
  auto invalid = [&]() {
    return arrow::Status::Invalid("invalid value for type ", type.ToString());
  };
  switch (type.id()) {
    case arrow::Type::STRING:
      if (!v.IsString()) return invalid();
      return static_cast<arrow::StringBuilder&>(builder).Append(
          v.GetString(), v.GetStringLength());
    case arrow::Type::BINARY:
      if (!v.IsString()) return invalid();
      return static_cast<arrow::BinaryBuilder&>(builder).Append(
          v.GetString(), v.GetStringLength());
    case arrow::Type::INT8:
    case arrow::Type::INT16:
    case arrow::Type::INT32:
    case arrow::Type::INT64: {
      if (!v.IsInt64()) return invalid();
      auto x = v.GetInt64();
      if (type.id() == arrow::Type::INT8)
        return static_cast<arrow::Int8Builder&>(builder).Append(x);
      if (type.id() == arrow::Type::INT16)
        return static_cast<arrow::Int16Builder&>(builder).Append(x);
      if (type.id() == arrow::Type::INT32)
        return static_cast<arrow::Int32Builder&>(builder).Append(x);
      return static_cast<arrow::Int64Builder&>(builder).Append(x);
    }
    case arrow::Type::FLOAT:
      if (!v.IsNumber()) return invalid();
      return static_cast<arrow::FloatBuilder&>(builder).Append(v.GetDouble());
    case arrow::Type::DOUBLE:
      if (!v.IsNumber()) return invalid();
      return static_cast<arrow::DoubleBuilder&>(builder).Append(v.GetDouble());
    case arrow::Type::BOOL:
      if (!v.IsBool()) return invalid();
      return static_cast<arrow::BooleanBuilder&>(builder).Append(v.GetBool());
    case arrow::Type::DATE32: {
      auto days = v.IsString() ? parse_date(v.GetString()) : std::nullopt;
      if (!days) return invalid();
      return static_cast<arrow::Date32Builder&>(builder).Append(*days);
    }
    case arrow::Type::TIMESTAMP: {
      auto micros = v.IsString() ? parse_timestamp(v.GetString()) : std::nullopt;
      if (!micros) return invalid();
      return static_cast<arrow::TimestampBuilder&>(builder).Append(*micros);
    }
    case arrow::Type::DECIMAL128: {
      if (!v.IsString()) return invalid();
      auto& dec = static_cast<const arrow::Decimal128Type&>(type);
      arrow::Decimal128 value;
      int32_t precision, scale;
      ARROW_RETURN_NOT_OK(arrow::Decimal128::FromString(v.GetString(), &value,
                                                        &precision, &scale));
      ARROW_ASSIGN_OR_RAISE(value, value.Rescale(scale, dec.scale()));
      return static_cast<arrow::Decimal128Builder&>(builder).Append(value);
    }
    default:
      return arrow::Status::NotImplemented("type ", type.ToString());
  }
}

arrow::Result<std::shared_ptr<arrow::Table>> build_table(
    const std::shared_ptr<arrow::Schema>& schema,
    const std::vector<const rapidjson::Value*>& rows) {
  arrow::ArrayVector columns;
  for (auto& field : schema->fields()) {
    std::unique_ptr<arrow::ArrayBuilder> builder;
    ARROW_RETURN_NOT_OK(
        arrow::MakeBuilder(arrow::default_memory_pool(), field->type(),
                           &builder));
    for (auto row : rows) {
      auto value = find_member(*row, field->name());
      if (!value || value->IsNull()) {
        if (!field->nullable()) {
          return arrow::Status::Invalid("column ", field->name(),
                                        " is not nullable");
        }
        ARROW_RETURN_NOT_OK(builder->AppendNull());
      } else {
        auto status = append_value(*builder, *field->type(), *value);
        if (!status.ok()) {
          return status.WithMessage("column ", field->name(), ": ",
                                    status.message());
        }
      }
    }
    std::shared_ptr<arrow::Array> array;
    ARROW_RETURN_NOT_OK(builder->Finish(&array));
    columns.push_back(array);
  }
  return arrow::Table::Make(schema, columns,
                            static_cast<int64_t>(rows.size()));
}

int64_t to_micros(int64_t value, arrow::TimeUnit::type unit) {
  switch (unit) {
    case arrow::TimeUnit::SECOND:
      return value * 1000000;
    case arrow::TimeUnit::MILLI:
      return value * 1000;
    case arrow::TimeUnit::MICRO:
      return value;
    case arrow::TimeUnit::NANO:
      // floor (INT96 timestamps of Spark are read as nanoseconds)
      return value >= 0 ? value / 1000 : -((-value + 999) / 1000);
  }
  return value;
}

rapidjson::Value json_value(const arrow::Array& array, int64_t i,
                            Json::AllocatorType& a) {
  if (array.IsNull(i)) {
    return rapidjson::Value(rapidjson::kNullType);
  }
  auto str = [&a](const std::string& s) { return rapidjson::Value(s.c_str(), s.size(), a); };
  switch (array.type_id()) {
    case arrow::Type::STRING:
      return str(static_cast<const arrow::StringArray&>(array).GetString(i));
    case arrow::Type::LARGE_STRING:
      return str(
          static_cast<const arrow::LargeStringArray&>(array).GetString(i));
    case arrow::Type::BINARY:
      return str(static_cast<const arrow::BinaryArray&>(array).GetString(i));
    case arrow::Type::INT8:
      return rapidjson::Value(static_cast<const arrow::Int8Array&>(array).Value(i));
    case arrow::Type::INT16:
      return rapidjson::Value(static_cast<const arrow::Int16Array&>(array).Value(i));
    case arrow::Type::INT32:
      return rapidjson::Value(static_cast<const arrow::Int32Array&>(array).Value(i));
    case arrow::Type::INT64:
      return rapidjson::Value(static_cast<int64_t>(
          static_cast<const arrow::Int64Array&>(array).Value(i)));
    case arrow::Type::FLOAT:
    case arrow::Type::DOUBLE: {
      double x = array.type_id() == arrow::Type::FLOAT
                     ? static_cast<const arrow::FloatArray&>(array).Value(i)
                     : static_cast<const arrow::DoubleArray&>(array).Value(i);
      if (!std::isfinite(x)) {
        return str(std::isnan(x) ? "NaN" : (x > 0 ? "Infinity" : "-Infinity"));
      }
      return rapidjson::Value(x);
    }
    case arrow::Type::BOOL:
      return rapidjson::Value(
          static_cast<const arrow::BooleanArray&>(array).Value(i));
    case arrow::Type::DATE32:
      return str(
          format_date(static_cast<const arrow::Date32Array&>(array).Value(i)));
    case arrow::Type::DATE64:
      return str(format_date(
          static_cast<const arrow::Date64Array&>(array).Value(i) / 86400000));
    case arrow::Type::TIMESTAMP: {
      auto& type = static_cast<const arrow::TimestampType&>(*array.type());
      auto value = static_cast<const arrow::TimestampArray&>(array).Value(i);
      return str(format_timestamp(to_micros(value, type.unit())));
    }
    case arrow::Type::DECIMAL128:
      return str(
          static_cast<const arrow::Decimal128Array&>(array).FormatValue(i));
    default: {
      auto scalar = array.GetScalar(i);
      return str(scalar.ok() ? (*scalar)->ToString() : "?");
    }
  }
}

// rows of an Arrow table as JSON objects
rapidjson::Value json_rows(const std::shared_ptr<arrow::Table>& table,
                           Json::AllocatorType& a) {
  rapidjson::Value rows(rapidjson::kArrayType);
  if (!table) {
    return rows;
  }
  auto combined = table->CombineChunks();
  if (!combined.ok()) {
    return rows;
  }
  auto& t = *combined;
  for (int64_t r = 0; r < t->num_rows(); r++) {
    rapidjson::Value row(rapidjson::kObjectType);
    for (int c = 0; c < t->num_columns(); c++) {
      auto& chunks = t->column(c)->chunks();
      rapidjson::Value value(rapidjson::kNullType);
      if (!chunks.empty()) {
        value = json_value(*chunks[0], r, a);
      }
      row.AddMember(rapidjson::Value(t->field(c)->name().c_str(), a), value, a);
    }
    rows.PushBack(row, a);
  }
  return rows;
}

std::string key_string(const rapidjson::Value& v) {
  if (v.IsString()) return v.GetString();
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  v.Accept(writer);
  return buffer.GetString();
}

// ---- operations -----------------------------------------------------------

struct OpResult {
  bool ok = true;
  bool writes = false;
  std::string error;
};

void configure(Manager& tm, const Options& opts) {
  tm.set_protocol_options(opts.protocol);
  tm.set_global_log_paths(opts.global_log + "log.txt",
                          opts.global_log + "log.txt.tmp");
}

void print_rows(Manager& tm, uint32_t id, const std::string& name,
                const std::shared_ptr<arrow::Table>& rows,
                const char* event = "rows") {
  Json d;
  d.SetObject();
  auto& a = d.GetAllocator();
  d.AddMember("event", rapidjson::Value(event, a), a);
  d.AddMember("table", rapidjson::Value(name.c_str(), a), a);
  d.AddMember("read_version", rapidjson::Value(tm.tables[id]->read_version), a);
  d.AddMember("files",
              static_cast<uint64_t>(tm.tables[id]->delta_log->get_list().size()),
              a);
  auto unsupported = tm.tables[id]->delta_log->get_unsupported();
  d.AddMember("unsupported", rapidjson::Value(unsupported.c_str(), a), a);
  d.AddMember("rows", json_rows(rows, a), a);
  print(d);
}

// delete/update a row: the file containing the key is replaced by a file
// with the remaining (updated) rows
OpResult rewrite_file(Manager& tm, StorageConnector::Connector& conn,
                      uint32_t id, const std::string& key,
                      const rapidjson::Value* set) {
  OpResult result;
  std::string error;
  auto schema = table_schema(tm, id, error);
  if (!schema) {
    return {false, false, error};
  }
  // the rows of the key (all files), conformed to the table schema by the
  // library; remove_file(key) keeps the other rows of their files
  std::string path;
  auto file = tm.read_file_as_table(id, key, path);
  if (!file) {
    return {false, false, "key not found: " + key};
  }
  Json d;
  auto rows = json_rows(file, d.GetAllocator());

  if (!tm.remove_file(id, static_cast<const std::string&>(key))) {
    return {false, false, "remove_file failed for " + key};
  }

  auto first = schema->field(0)->name();
  std::vector<const rapidjson::Value*> remaining;
  for (auto& row : rows.GetArray()) {
    auto value = find_member(row, first);
    if (value && !value->IsNull() && key_string(*value) == key) {
      if (!set) {
        continue;
      }
      for (auto it = set->MemberBegin(); it != set->MemberEnd(); ++it) {
        auto target = row.FindMember(it->name);
        rapidjson::Value copy(it->value, d.GetAllocator());
        if (target != row.MemberEnd()) {
          target->value = copy;
        } else {
          row.AddMember(rapidjson::Value(it->name, d.GetAllocator()), copy,
                        d.GetAllocator());
        }
      }
    }
    remaining.push_back(&row);
  }
  if (!remaining.empty()) {
    auto table = build_table(schema, remaining);
    if (!table.ok()) {
      return {false, false, table.status().ToString()};
    }
    if (!tm.add_file(id, *table, key)) {
      return {false, false, "add_file failed"};
    }
  }
  result.writes = true;
  return result;
}

OpResult run_op(Manager& tm, StorageConnector::Connector& conn, uint32_t id,
                const rapidjson::Value& op) {
  std::string kind = op["op"].GetString();
  std::string name = op["table"].GetString();
  std::string key = op.HasMember("key") ? key_string(op["key"]) : "";

  if (kind == "read") {
    auto rows = tm.read_table(id);
    print_rows(tm, id, name, rows);
    // read_table returns nullptr for an empty table and on errors
    if (!rows && !tm.tables[id]->delta_log->get_list().empty()) {
      return {false, false, "read_table failed (see stderr)"};
    }
    return {};
  }
  if (kind == "lookup") {
    std::string path;
    auto rows = tm.read_file_as_table(id, key, path);
    print_rows(tm, id, name, rows, "lookup");
    if (!rows) {
      return {false, false, "read_file_as_table found no file for " + key};
    }
    return {};
  }
  if (kind == "insert") {
    std::string error;
    auto schema = table_schema(tm, id, error);
    if (!schema) {
      return {false, false, error};
    }
    bool per_row = !op.HasMember("file_per_row") || op["file_per_row"].GetBool();
    std::vector<std::vector<const rapidjson::Value*>> files;
    for (auto& row : op["rows"].GetArray()) {
      if (per_row || files.empty()) {
        files.emplace_back();
      }
      files.back().push_back(&row);
    }
    for (auto& rows : files) {
      auto table = build_table(schema, rows);
      if (!table.ok()) {
        return {false, false, table.status().ToString()};
      }
      auto first = find_member(*rows[0], schema->field(0)->name());
      if (!tm.add_file(id, *table, first ? key_string(*first) : "")) {
        return {false, false, "add_file failed"};
      }
    }
    return {true, true, ""};
  }
  if (kind == "update") {
    return rewrite_file(tm, conn, id, key, &op["set"]);
  }
  if (kind == "delete") {
    return rewrite_file(tm, conn, id, key, nullptr);
  }
  if (kind == "remove-file") {
    if (!tm.remove_file(id, static_cast<const std::string&>(key))) {
      return {false, false, "remove_file found no file for " + key};
    }
    return {true, true, ""};
  }
  return {false, false, "unknown operation " + kind};
}

int run_txn(StorageConnector::Connector& conn, const Options& opts,
            const std::string& ops_json) {
  auto ops = LHHelpers::readJSON_rapid(ops_json.c_str());
  if (!ops->IsArray() || ops->Empty()) {
    print_result("error", "ops must be a non-empty list");
    return 1;
  }
  std::vector<std::string> tables;
  for (auto& op : ops->GetArray()) {
    if (!op.IsObject() || !op.HasMember("op") || !op.HasMember("table")) {
      print_result("error", "every operation needs 'op' and 'table'");
      return 1;
    }
    std::string name = op["table"].GetString();
    if (std::find(tables.begin(), tables.end(), name) == tables.end()) {
      tables.push_back(name);
    }
  }

  auto txn_id =
      opts.txn_id ? opts.txn_id : LHTransactions::unique_transaction_id();
  Manager tm(opts.levels, tables[0], &conn, txn_id);
  configure(tm, opts);
  if (!tm.begin_transaction_ycsb()) {
    print_result("error", "begin failed");
    return 1;
  }
  for (size_t i = 1; i < tables.size(); i++) {
    if (!tm.open_new_table(tables[i])) {
      print_result("error", "unable to open " + tables[i]);
      return 1;
    }
  }

  bool writes = false;
  for (auto& op : ops->GetArray()) {
    std::string name = op["table"].GetString();
    auto id = static_cast<uint32_t>(
        std::find(tables.begin(), tables.end(), name) - tables.begin());
    auto result = run_op(tm, conn, id, op);
    if (!result.ok) {
      tm.abort();
      print_result("aborted", result.error);
      return 3;
    }
    writes |= result.writes;
  }

  std::string action = opts.crash ? "crash" : "commit";
  if (opts.pause) {
    Json d;
    d.SetObject();
    d.AddMember("event", "paused", d.GetAllocator());
    print(d);
    if (!std::getline(std::cin, action)) {
      action = "abort";
    }
  }
  if (action == "crash") {
    // a crashed client: markers and sublogs stay behind
    std::cout << R"({"event":"result","status":"crashed"})" << std::endl;
    std::_Exit(0);
  }
  if (action == "abort") {
    tm.abort();
    print_result("aborted", "aborted by the harness");
    return 3;
  }

  // a transaction without writes ends with the read-only hint (abort)
  bool committed = tm.commit(!writes);
  Json extra;
  extra.SetObject();
  auto& a = extra.GetAllocator();
  extra.AddMember("redos", rapidjson::Value(tm.times_redo), a);
  rapidjson::Value versions(rapidjson::kObjectType);
  for (auto& table : tm.tables) {
    versions.AddMember(rapidjson::Value(table->base_path.c_str(), a),
                       rapidjson::Value(table->marker_version), a);
  }
  extra.AddMember("versions", versions, a);
  if (!writes) {
    print_result("ok", "", &extra);
    return 0;
  }
  print_result(committed ? "committed" : "aborted", "", &extra);
  return committed ? 0 : 3;
}

const char* table_result(Manager::TableResult result) {
  switch (result) {
    case Manager::TableResult::Ok:
      return "ok";
    case Manager::TableResult::AlreadyExists:
      return "already_exists";
    case Manager::TableResult::NotEmpty:
      return "not_empty";
    case Manager::TableResult::NotFound:
      return "not_found";
    case Manager::TableResult::Invalid:
      return "invalid";
    case Manager::TableResult::Error:
      return "error";
  }
  return "error";
}

int run(int argc, char** argv) {
  Options opts;
  int i = 1;
  for (; i < argc && std::string(argv[i]).rfind("--", 0) == 0; i++) {
    std::string arg = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error("missing value for " + arg);
      }
      return argv[++i];
    };
    if (arg == "--levels") {
      auto levels = next();
      opts.levels = {false, false, false};
      if (levels != "none") {
        for (char c : levels) {
          if (c < '0' || c > '2') throw std::runtime_error("invalid --levels");
          opts.levels[c - '0'] = true;
        }
      }
    } else if (arg == "--isolation") {
      auto value = next();
      opts.protocol.isolation = value == "snapshot"
                                    ? LHTransactions::Isolation::Snapshot
                                    : LHTransactions::Isolation::Serializable;
    } else if (arg == "--lease-ms") {
      opts.protocol.marker_lease_ms = std::stoul(next());
    } else if (arg == "--grace-ms") {
      opts.protocol.marker_grace_ms = std::stoul(next());
    } else if (arg == "--max-redos") {
      opts.protocol.max_redos = std::stoul(next());
    } else if (arg == "--repair") {
      opts.protocol.global_log_repair = true;
    } else if (arg == "--repair-after") {
      opts.protocol.global_log_repair_after = std::stoul(next());
    } else if (arg == "--check-at-begin") {
      opts.protocol.global_log_check_at_begin = true;
    } else if (arg == "--checkpoints") {
      opts.protocol.checkpoints = next() != "0";
    } else if (arg == "--checkpoint-interval") {
      opts.protocol.checkpoint_interval = std::stoul(next());
    } else if (arg == "--global-log") {
      opts.global_log = next();
    } else if (arg == "--txn-id") {
      opts.txn_id = std::stoul(next());
    } else if (arg == "--pause") {
      opts.pause = true;
    } else if (arg == "--crash") {
      opts.crash = true;
    } else {
      throw std::runtime_error("unknown option " + arg);
    }
  }
  if (i >= argc) {
    throw std::runtime_error("missing command (see compat.cpp)");
  }
  std::string command = argv[i++];
  std::vector<std::string> args(argv + i, argv + argc);

  StorageConnector::MinIOConfig config;
  config.endpointURI = env("LV_S3_ENDPOINT", "minio:9000");
  config.user = env("LV_S3_USER", "admin");
  config.passwd = env("LV_S3_PASSWD", "password");
  config.region = env("LV_S3_REGION", "us-east-1");
  config.default_bucket = env("LV_S3_BUCKET", "compat");
  StorageConnector::MinIOConnector conn(&config);

  if (command == "txn" && args.size() == 1) {
    return run_txn(conn, opts, args[0]);
  }
  if (command == "read" && args.size() == 1) {
    return run_txn(conn, opts,
                   R"([{"op":"read","table":")" + args[0] + R"("}])");
  }
  if (args.empty()) {
    throw std::runtime_error("missing table for " + command);
  }
  std::string path = args[0];
  auto txn_id =
      opts.txn_id ? opts.txn_id : LHTransactions::unique_transaction_id();
  Manager tm(opts.levels, path, &conn, txn_id);
  configure(tm, opts);

  if (command == "create" && (args.size() == 2 || args.size() == 3)) {
    std::string error;
    auto schema = create_schema(args[1], error);
    if (!schema) {
      print_result("error", error);
      return 1;
    }
    Manager::TableOptions options;
    if (args.size() == 3) {
      options.description = args[2];
    }
    auto result = tm.create_table(path, *schema, options, &error);
    print_result(table_result(result), error);
    return result == Manager::TableResult::Ok ? 0 : 1;
  }
  if (command == "drop" && args.size() == 1) {
    std::string error;
    auto result = tm.drop_table(path, &error);
    print_result(table_result(result), error);
    return result == Manager::TableResult::Ok ? 0 : 1;
  }
  if (command == "checkpoint" && args.size() == 1) {
    if (!tm.begin_transaction_ycsb()) {
      print_result("error", "begin failed");
      return 1;
    }
    bool ok = tm.create_checkpoint(0);
    auto unsupported = tm.tables[0]->delta_log->get_unsupported();
    tm.commit(true);
    print_result(ok ? "ok" : "error",
                 ok ? "" : "create_checkpoint failed " + unsupported);
    return ok ? 0 : 1;
  }
  throw std::runtime_error("invalid command " + command);
}

}  // namespace

int main(int argc, char** argv) {
  Aws::SDKOptions sdk;
  Aws::InitAPI(sdk);
  int code;
  try {
    code = run(argc, argv);
  } catch (const std::exception& e) {
    print_result("error", e.what());
    code = 1;
  }
  Aws::ShutdownAPI(sdk);
  return code;
}
