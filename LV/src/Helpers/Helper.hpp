#pragma once
#include <arrow/api.h>
#include <arrow/io/buffered.h>
#include <arrow/io/memory.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/schema.h>
#include <parquet/stream_reader.h>

#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "arrow/util/type_fwd.h"
#include "parquet/arrow/writer.h"
#include "rapidjson/include/rapidjson/document.h"
#include "rapidjson/include/rapidjson/stringbuffer.h"
#include "rapidjson/include/rapidjson/writer.h"

namespace LHHelpers {
enum ColumnTypes { INT32, STRING };

struct ParquetPair {
  char* first;
  uint64_t second;

  ParquetPair(char* data, uint64_t size) {
    first = data;
    second = size;
  }

  ~ParquetPair() { std::free(first); }
};

std::string readParquet(const char* data, uint64_t length);

std::shared_ptr<arrow::Table> readParquetAsTable(const char* data,
                                                 uint64_t length);

// milliseconds since the Unix epoch (Delta timestamps)
int64_t now_ms();

// statistics of one column in the Delta stats format
struct ColumnStatistics {
  std::string name;
  bool has_min_max = false;  // false: type without min/max or only nulls
  bool numeric = false;      // min/max are JSON numbers (else strings)
  std::string min;
  std::string max;
  int64_t null_count = 0;
};

// Delta file statistics: numRecords (-1 = unknown) and per-column stats
struct TableStatistics {
  int64_t num_records = -1;
  std::vector<ColumnStatistics> columns;
};

// statistics of all top-level columns (min/max for strings, integers and
// floating point columns without NaN)
TableStatistics computeStatistics(const arrow::Table& table);

// number of rows from the footer of a Parquet file (-1 if unreadable)
int64_t parquetNumRows(const char* data, uint64_t length);

// an add action of a Delta checkpoint
struct CheckpointEntry {
  std::string path;
  std::string stats;  // "" if the checkpoint has no JSON stats
  int64_t size = -1;
  int64_t modification_time = -1;  // -1: unknown (written as now)
  // JSON objects ("" if absent: no partition values / tags)
  std::string partition_values;
  std::string tags;
};

// a remove action (tombstone) of a Delta checkpoint
struct CheckpointRemove {
  std::string path;
  int64_t deletion_timestamp = 0;
  int64_t size = -1;  // -1: unknown (no extendedFileMetadata)
  std::string partition_values;
};

// the latest txn action of an application (streaming writers)
struct CheckpointTxn {
  std::string app_id;
  int64_t version = 0;
  int64_t last_updated = -1;  // -1: absent
};

// the latest domainMetadata action of a domain that was not removed
struct CheckpointDomain {
  std::string domain;
  std::string configuration;
};

// content of a Delta checkpoint (or of a checkpoint to write)
struct CheckpointContent {
  std::vector<CheckpointEntry> adds;
  std::vector<CheckpointRemove> removes;
  std::vector<CheckpointTxn> txns;
  std::vector<CheckpointDomain> domains;
  // JSON of the protocol/metaData action ("" if absent)
  std::string protocol;
  std::string metadata;
  // non-empty if the checkpoint contains actions LakeVilla does not track
  std::string unsupported;
};

std::optional<CheckpointContent> readDLCheckpointContent(const char* data,
                                                         uint64_t length);

// all add actions of a Delta checkpoint file; std::nullopt if unreadable
std::optional<std::vector<CheckpointEntry>> readDLCheckpointEntries(
    const char* data, uint64_t length);

// paths of all add actions of a Delta checkpoint file
std::vector<std::string> readDLCheckpoint(const char* data, uint64_t length);

std::unique_ptr<ParquetPair> encodeAsParquet(std::vector<std::string> names,
                                             std::vector<int32_t> values);

std::unique_ptr<ParquetPair> encodeAsParquet(
    std::shared_ptr<arrow::Table> table);

// a checkpoint in the Delta checkpoint schema (protocol, metaData, add,
// remove); protocol and metadata are required
std::unique_ptr<ParquetPair> generateCheckpoint(
    const CheckpointContent& content);

// chunk_size: rows per row group
std::string encodeAsParquet_str(std::shared_ptr<arrow::Table> table,
                                int64_t chunk_size = 3);

void writeParquetTest();

std::unique_ptr<rapidjson::Document> readJSON_rapid(const char* data);

rapidjson::StringBuffer encodeAsJSON_rapid(
    std::unique_ptr<rapidjson::Document>& content);

std::shared_ptr<arrow::Table> arrow_table_builder();

}  // namespace LHHelpers