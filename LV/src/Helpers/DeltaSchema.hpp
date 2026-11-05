#pragma once

#include <arrow/api.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace LHHelpers {

// Delta Lake schema of an Arrow schema, as Spark writes it into
// metaData.schemaString (CREATE TABLE ... USING DELTA)
struct DeltaSchema {
  std::string schema_string;
  // table features the schema needs (e.g. "timestampNtz"); empty: protocol
  // (1, 2) as for every table Spark creates without such types
  std::vector<std::string> features;
};

// Type mapping (Arrow -> Delta): utf8/large_utf8 -> string, int8/16/32/64 ->
// byte/short/integer/long, float/double, bool -> boolean, binary/large_binary/
// fixed_size_binary -> binary, date32 -> date, timestamp with time zone ->
// timestamp, timestamp without time zone -> timestamp_ntz, decimal128(p, s)
// -> decimal(p,s), list/large_list -> array, map, struct; dictionaries use
// their value type. Field metadata becomes the Delta field metadata (e.g.
// "comment", "__CHAR_VARCHAR_TYPE_STRING").
//
// std::nullopt (and *error) for types without a Delta equivalent (unsigned
// integers, time, intervals, ...), an empty schema, duplicate column names
// (case-insensitive, as Spark) and names with characters Delta does not allow
// without column mapping (" ,;{}()\n\t=").
std::optional<DeltaSchema> to_delta_schema(const arrow::Schema& schema,
                                           std::string* error = nullptr);

// Arrow schema of a Delta schema (metaData.schemaString), the inverse of
// to_delta_schema: string/char/varchar -> utf8, byte/short/integer/long ->
// int8/16/32/64, float/double, boolean, binary, date -> date32, timestamp ->
// timestamp[us, UTC], timestamp_ntz -> timestamp[us], decimal(p,s) ->
// decimal128(p,s), void -> null, array -> list<element>, map ->
// map<key, value>, struct -> struct. Field metadata is kept (values that are
// not strings as their JSON text, e.g. delta.columnMapping.id).
// std::nullopt (and *error) for invalid JSON and unknown types.
std::optional<std::shared_ptr<arrow::Schema>> from_delta_schema(
    const std::string& schema_string, std::string* error = nullptr);

// the logical schema of a Delta table (metaData action)
struct TableSchema {
  // all columns in table order (partition columns included)
  std::shared_ptr<arrow::Schema> schema;
  std::vector<std::string> partition_columns;
  // delta.columnMapping.mode: data files use the physical names
  // (field metadata delta.columnMapping.physicalName)
  bool column_mapping = false;
};

// TableSchema of a metaData action (JSON); std::nullopt (and *error) if the
// schema cannot be parsed
std::optional<TableSchema> table_schema(const std::string& metadata_json,
                                        std::string* error = nullptr);

struct ConformOptions {
  // match file columns by delta.columnMapping.physicalName
  bool physical_names = false;
  // values of the partition columns (Delta serialization; std::nullopt =
  // null) by column name; partition columns are not stored in the files
  const std::vector<std::pair<std::string, std::optional<std::string>>>*
      partition_values = nullptr;
  // writes: columns that are not part of the schema are an error (reads drop
  // them)
  bool reject_extra_columns = false;
};

// Converts a table read from (or written to) a data file to the table
// schema: columns are matched by name (case-insensitively, as Delta does),
// ordered and named as in the schema; missing columns are null (an error for
// NOT NULL columns); types are converted to the schema's types (timestamps
// without time zone or in another unit, wider integers and floats, decimal
// precision/scale, large/dictionary strings, nested fields by name).
// Timestamps finer than microseconds are truncated (floor), as Spark does.
// Nullability follows the schema; nulls in a NOT NULL column are an error.
// Columns that already have the schema's type are reused without a copy.
arrow::Result<std::shared_ptr<arrow::Table>> conform_to_schema(
    const std::shared_ptr<arrow::Table>& table, const arrow::Schema& schema,
    const ConformOptions& options = {});

// random UUID (version 4), e.g. for metaData.id and commitInfo.txnId
std::string random_uuid();

}  // namespace LHHelpers
