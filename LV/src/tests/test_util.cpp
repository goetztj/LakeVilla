#include "test_util.hpp"

#include <algorithm>
#include <sstream>

#include "Helpers/Helper.hpp"

namespace lvtest {

std::shared_ptr<arrow::Table> ycsb_table(const std::string& key,
                                         const std::string& value) {
  arrow::FieldVector fields;
  arrow::ArrayVector columns;

  std::vector<std::string> names = {"YCSB_KEY"};
  for (int i = 0; i < 10; i++) {
    names.push_back("field" + std::to_string(i));
  }

  for (size_t i = 0; i < names.size(); i++) {
    arrow::StringBuilder builder;
    auto status = builder.Append(i == 0 ? key : value);
    std::shared_ptr<arrow::Array> array;
    status = builder.Finish(&array);
    fields.push_back(arrow::field(names[i], arrow::utf8()));
    columns.push_back(array);
  }

  return arrow::Table::Make(arrow::schema(fields), columns, 1);
}

std::string ycsb_parquet(const std::string& key, const std::string& value) {
  return LHHelpers::encodeAsParquet_str(ycsb_table(key, value));
}

std::string log_name(uint64_t version) {
  auto v = std::to_string(version);
  return std::string(20 - v.size(), '0') + v + ".json";
}

std::string add_action(const std::string& file, uint64_t size,
                       const std::string& key) {
  std::stringstream stats;
  stats << R"({\"numRecords\":1,\"minValues\":{\"YCSB_KEY\":\")" << key
        << R"(\"},\"maxValues\":{\"YCSB_KEY\":\")" << key
        << R"(\"},\"nullCount\":{\"YCSB_KEY\":0}})";

  std::stringstream out;
  out << R"({"add":{"path":")" << file
      << R"(","partitionValues":{},"size":)" << size
      << R"(,"modificationTime":1700000000000,"dataChange":true,"stats":")"
      << stats.str() << R"("}})";
  return out.str();
}

std::string remove_action(const std::string& file) {
  return R"({"remove":{"path":")" + file +
         R"(","deletionTimestamp":1700000000000,"dataChange":true}})";
}

std::string protocol_and_metadata() {
  std::string schema =
      R"({\"type\":\"struct\",\"fields\":[{\"name\":\"YCSB_KEY\",\"type\":\"string\",\"nullable\":true,\"metadata\":{}})";
  for (int i = 0; i < 10; i++) {
    schema += R"(,{\"name\":\"field)" + std::to_string(i) +
              R"(\",\"type\":\"string\",\"nullable\":true,\"metadata\":{}})";
  }
  schema += "]}";

  return std::string(
             R"({"protocol":{"minReaderVersion":1,"minWriterVersion":2}})") +
         "\n" +
         R"({"metaData":{"id":"lv-test","format":{"provider":"parquet","options":{}},"schemaString":")" +
         schema +
         R"(","partitionColumns":[],"configuration":{},"createdTime":1700000000000}})";
}

std::vector<std::string> create_ycsb_table(
    StorageConnector::Connector& conn, const std::string& base,
    const std::vector<std::string>& keys) {
  std::vector<std::string> actions = {protocol_and_metadata()};
  std::vector<std::string> files;

  for (size_t i = 0; i < keys.size(); i++) {
    auto file = "part-0000" + std::to_string(i) + "-test.snappy.parquet";
    auto content = ycsb_parquet(keys[i]);
    conn.write(base + file, content.data(), content.size());
    actions.push_back(add_action(file, content.size(), keys[i]));
    files.push_back(file);
  }

  write_version(conn, base, 0, actions);
  return files;
}

void write_version(StorageConnector::Connector& conn, const std::string& base,
                   uint64_t version, const std::vector<std::string>& actions) {
  std::stringstream content;
  for (size_t i = 0; i < actions.size(); i++) {
    if (i > 0) {
      content << "\n";
    }
    content << actions[i];
  }
  auto str = content.str();
  conn.write(base + "_delta_log/" + log_name(version), str.data(), str.size());
}

std::vector<std::string> split_lines(const std::string& content) {
  std::vector<std::string> lines;
  std::stringstream stream(content);
  for (std::string line; std::getline(stream, line);) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::vector<std::string> keys_of(const std::shared_ptr<arrow::Table>& table) {
  std::vector<std::string> keys;
  if (!table) {
    return keys;
  }
  auto column = table->GetColumnByName("YCSB_KEY");
  for (auto& chunk : column->chunks()) {
    auto strings = std::static_pointer_cast<arrow::StringArray>(chunk);
    for (int64_t i = 0; i < strings->length(); i++) {
      keys.push_back(strings->GetString(i));
    }
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

}  // namespace lvtest
