#include "DeltaLogEntry.hpp"

#include <algorithm>
#include <cctype>

using namespace LHExecutor::LHDL;

DeltaLogEntry::DeltaLogEntry(DLOperation operation) : operation(operation) {
  this->min.clear();
  this->max.clear();
}

namespace {
// string representation of a statistics value; every value keeps its position
std::string stat_to_string(const rapidjson::Value& value) {
  if (value.IsString()) {
    return value.GetString();
  }
  if (value.IsInt()) {
    return std::to_string(value.GetInt());
  }
  if (value.IsInt64()) {
    return std::to_string(value.GetInt64());
  }
  if (value.IsUint64()) {
    return std::to_string(value.GetUint64());
  }
  if (value.IsNull()) {
    return "";
  }
  // doubles, booleans and nested (struct) statistics as JSON text
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  value.Accept(writer);
  return buffer.GetString();
}

void read_stats(const rapidjson::Document& doc, const char* name,
                std::vector<std::string>& out,
                std::vector<std::string>& columns) {
  if (!doc.HasMember(name) || !doc[name].IsObject()) {
    return;
  }
  for (auto itr = doc[name].MemberBegin(); itr != doc[name].MemberEnd();
       ++itr) {
    out.push_back(stat_to_string(itr->value));
    columns.push_back(itr->name.GetString());
  }
}

bool same_name(const std::string& a, const std::string& b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(),
                    [](unsigned char x, unsigned char y) {
                      return std::tolower(x) == std::tolower(y);
                    });
}

const std::string* value_of(const std::vector<std::string>& columns,
                            const std::vector<std::string>& values,
                            const std::string& column) {
  for (size_t i = 0; i < columns.size() && i < values.size(); i++) {
    if (columns[i] == column) {
      return &values[i];
    }
  }
  for (size_t i = 0; i < columns.size() && i < values.size(); i++) {
    if (same_name(columns[i], column)) {
      return &values[i];
    }
  }
  return nullptr;
}
}  // namespace

DeltaLogEntry::DeltaLogEntry(DLOperation operation, const char* stats,
                             int64_t size)
    : operation(operation), size(size) {
  if (!stats || stats[0] == '\0') {
    return;
  }
  this->stats = stats;

  auto sub_json = LHHelpers::readJSON_rapid(stats);
  if (sub_json->HasParseError() || !sub_json->IsObject()) {
    std::cerr << "ignoring invalid statistics: " << stats << std::endl;
    return;
  }

  read_stats(*sub_json, "minValues", this->min, this->min_columns);
  read_stats(*sub_json, "maxValues", this->max, this->max_columns);
  auto records = sub_json->FindMember("numRecords");
  if (records != sub_json->MemberEnd() && records->value.IsInt64()) {
    this->num_records = records->value.GetInt64();
  }
}

DeltaLogEntry::DeltaLogEntry(DLOperation operation,
                             std::vector<std::string>& min,
                             std::vector<std::string>& max)
    : operation(operation) {
  this->min = min;
  this->max = max;
}

DeltaLogEntry::DeltaLogEntry(DLOperation operation,
                             std::vector<std::string> columns,
                             std::vector<std::string> min,
                             std::vector<std::string> max, int64_t num_records)
    : operation(operation),
      min(std::move(min)),
      max(std::move(max)),
      min_columns(columns),
      max_columns(std::move(columns)),
      num_records(num_records) {}

const std::string* DeltaLogEntry::min_of(const std::string& column) const {
  return value_of(this->min_columns, this->min, column);
}

const std::string* DeltaLogEntry::max_of(const std::string& column) const {
  return value_of(this->max_columns, this->max, column);
}

DeltaLogEntry DeltaLogEntry::from_action(DLOperation operation,
                                         const rapidjson::Value& action,
                                         int64_t version) {
  auto member = [&action](const char* name) -> const rapidjson::Value* {
    auto it = action.FindMember(name);
    return it != action.MemberEnd() ? &it->value : nullptr;
  };
  auto object_json = [](const rapidjson::Value* value) -> std::string {
    if (!value || !value->IsObject()) {
      return "";
    }
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    value->Accept(writer);
    return std::string(buffer.GetString(), buffer.GetSize());
  };

  auto stats = operation == DLOperation::ADD ? member("stats") : nullptr;
  auto size = member("size");
  DeltaLogEntry entry(operation,
                      stats && stats->IsString() ? stats->GetString() : nullptr,
                      size && size->IsInt64() ? size->GetInt64() : -1);
  if (operation == DLOperation::ADD) {
    auto time = member("modificationTime");
    if (time && time->IsInt64()) {
      entry.modification_time = time->GetInt64();
    }
  } else {
    auto time = member("deletionTimestamp");
    if (time && time->IsInt64()) {
      entry.deletion_timestamp = time->GetInt64();
    }
  }
  entry.partition_values = object_json(member("partitionValues"));
  entry.tags = object_json(member("tags"));
  entry.version = version;
  return entry;
}

void DeltaLogEntry::update(DLOperation operation, bool overwrite) {
  if (overwrite) {
    this->operation = operation;
    return;
  }

  switch (this->operation) {
    case DLOperation::ADD:
      this->ADDUpdate(operation);
      break;
    case DLOperation::DELETE:
      this->DELETEUpdate(operation);
      break;
    case DLOperation::METADATA:
      this->METADATAUpdate(operation);
      break;
    case DLOperation::NONE:
      this->NONEUpdate(operation);
      break;

    default:
      break;
  }
}

void DeltaLogEntry::ADDUpdate(DLOperation operation) {
  switch (operation) {
    case DLOperation::ADD:
      // added again - do nothing
      break;
    case DLOperation::DELETE:
      // entry removed - delete
      this->operation = DLOperation::DELETE;
      break;
    case DLOperation::METADATA:
      // incompatible in this version
      break;
    case DLOperation::NONE:
      break;
    default:
      break;
  }
}

void DeltaLogEntry::DELETEUpdate(DLOperation operation) {
  switch (operation) {
    case DLOperation::ADD:
      // added file - remove beats adding
      break;
    case DLOperation::DELETE:
      // entry removed again - do nothing
      break;
    case DLOperation::METADATA:
      // incompatible in this version
      break;
    case DLOperation::NONE:
      break;
    default:
      break;
  }
}

void DeltaLogEntry::METADATAUpdate(DLOperation operation) {
  switch (operation) {
    case DLOperation::ADD:
      // incompatible in this version
      break;
    case DLOperation::DELETE:
      // incompatible in this version
      break;
    case DLOperation::METADATA:
      // incompatible in this version
      break;
    case DLOperation::NONE:
      break;
    default:
      break;
  }
}

void DeltaLogEntry::NONEUpdate(DLOperation operation) {
  switch (operation) {
    case DLOperation::ADD:
      this->operation = DLOperation::ADD;
      break;
    case DLOperation::DELETE:
      this->operation = DLOperation::DELETE;
      break;
    case DLOperation::METADATA:
      this->operation = DLOperation::METADATA;
      break;
    case DLOperation::NONE:
      break;
    default:
      break;
  }
}