#pragma once
#include <arrow/api.h>
#include <arrow/io/buffered.h>
#include <arrow/io/memory.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/schema.h>
#include <parquet/stream_reader.h>

#include <iostream>
#include <memory>
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
  uint32_t second;

  ParquetPair(char* data, uint64_t size) {
    first = data;
    second = size;
  }

  ~ParquetPair() { std::free(first); }
};

std::string readParquet(const char* data, uint64_t length);

std::shared_ptr<arrow::Table> readParquetAsTable(const char* data,
                                                 uint64_t length);

std::vector<std::string> readDLCheckpoint(const char* data, uint64_t length);

std::unique_ptr<ParquetPair> encodeAsParquet(std::vector<std::string> names,
                                             std::vector<int32_t> values);

std::unique_ptr<ParquetPair> encodeAsParquet(
    std::shared_ptr<arrow::Table> table);

std::unique_ptr<ParquetPair> generateCheckpoint(std::vector<std::string> files);

std::string encodeAsParquet_str(std::shared_ptr<arrow::Table> table);

void writeParquetTest();

std::unique_ptr<rapidjson::Document> readJSON_rapid(const char* data);

rapidjson::StringBuffer encodeAsJSON_rapid(
    std::unique_ptr<rapidjson::Document>& content);

std::shared_ptr<arrow::Table> arrow_table_builder();

}  // namespace LHHelpers