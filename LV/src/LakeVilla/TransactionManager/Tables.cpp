// Creating and dropping Delta tables (as Spark's CREATE TABLE ... USING DELTA
// and DROP TABLE)
#include <algorithm>
#include <cctype>
#include <set>

#include "Helpers/DeltaSchema.hpp"
#include "ManagerDeltaLake.hpp"

namespace {

using TableResult = LHTransactions::TransactionManagerDeltaLake::TableResult;
using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

TableResult fail(std::string* error, TableResult result,
                 const std::string& message) {
  if (error) {
    *error = message;
  }
  std::cerr << message << std::endl;
  return result;
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return text;
}

// "wh/db.db/table" -> "wh/db.db/table/"
std::optional<std::string> table_prefix(const std::string& path) {
  if (path.empty() || path == "/" || path.front() == '/') {
    return std::nullopt;
  }
  return path.back() == '/' ? path : path + "/";
}

// the properties as a JSON object
void write_properties(
    Writer& w,
    const std::vector<std::pair<std::string, std::string>>& properties) {
  w.StartObject();
  for (auto& property : properties) {
    w.Key(property.first.c_str());
    w.String(property.second.c_str());
  }
  w.EndObject();
}

// version 0 of a new table, as Spark 3.5 / Delta 3.0 writes it
std::string create_table_commit(
    const LHHelpers::DeltaSchema& schema,
    const LHTransactions::TransactionManagerDeltaLake::TableOptions& options) {
  int64_t now = LHHelpers::now_ms();
  std::string lines;

  {
    rapidjson::StringBuffer properties_buffer;
    Writer properties_writer(properties_buffer);
    write_properties(properties_writer, options.properties);

    rapidjson::StringBuffer buffer;
    Writer w(buffer);
    w.StartObject();
    w.Key("commitInfo");
    w.StartObject();
    w.Key("timestamp");
    w.Int64(now);
    w.Key("operation");
    w.String("CREATE TABLE");
    w.Key("operationParameters");
    w.StartObject();
    w.Key("isManaged");
    w.String("false");
    w.Key("description");
    if (options.description.empty()) {
      w.Null();
    } else {
      w.String(options.description.c_str());
    }
    w.Key("partitionBy");
    w.String("[]");
    w.Key("properties");
    w.String(properties_buffer.GetString());
    w.EndObject();
    w.Key("isolationLevel");
    w.String("Serializable");
    w.Key("isBlindAppend");
    w.Bool(true);
    w.Key("operationMetrics");
    w.StartObject();
    w.EndObject();
    w.Key("engineInfo");
    w.String("LHTransactions 0.1 - Delta Lake 3.0");
    w.Key("txnId");
    w.String(LHHelpers::random_uuid().c_str());
    w.EndObject();
    w.EndObject();
    lines += std::string(buffer.GetString()) + "\n";
  }

  {
    rapidjson::StringBuffer buffer;
    Writer w(buffer);
    w.StartObject();
    w.Key("metaData");
    w.StartObject();
    w.Key("id");
    w.String(LHHelpers::random_uuid().c_str());
    if (!options.description.empty()) {
      w.Key("description");
      w.String(options.description.c_str());
    }
    w.Key("format");
    w.StartObject();
    w.Key("provider");
    w.String("parquet");
    w.Key("options");
    w.StartObject();
    w.EndObject();
    w.EndObject();
    w.Key("schemaString");
    w.String(schema.schema_string.c_str());
    w.Key("partitionColumns");
    w.StartArray();
    w.EndArray();
    w.Key("configuration");
    write_properties(w, options.properties);
    w.Key("createdTime");
    w.Int64(now);
    w.EndObject();
    w.EndObject();
    lines += std::string(buffer.GetString()) + "\n";
  }

  {
    rapidjson::StringBuffer buffer;
    Writer w(buffer);
    w.StartObject();
    w.Key("protocol");
    w.StartObject();
    if (schema.features.empty()) {
      w.Key("minReaderVersion");
      w.Int(1);
      w.Key("minWriterVersion");
      w.Int(2);
    } else {
      // table features (e.g. timestamp_ntz columns) need protocol (3, 7)
      w.Key("minReaderVersion");
      w.Int(3);
      w.Key("minWriterVersion");
      w.Int(7);
      for (const char* key : {"readerFeatures", "writerFeatures"}) {
        w.Key(key);
        w.StartArray();
        for (auto& feature : schema.features) {
          w.String(feature.c_str());
        }
        w.EndArray();
      }
    }
    w.EndObject();
    w.EndObject();
    lines += std::string(buffer.GetString()) + "\n";
  }
  return lines;
}

}  // namespace

LHTransactions::TransactionManagerDeltaLake::TableResult
LHTransactions::TransactionManagerDeltaLake::create_table(
    const std::string& path, const arrow::Schema& schema,
    const TableOptions& options, std::string* error) {
  auto prefix = table_prefix(path);
  if (!prefix) {
    return fail(error, TableResult::Invalid,
                "invalid table path '" + path + "'");
  }

  std::string schema_error;
  auto delta_schema = LHHelpers::to_delta_schema(schema, &schema_error);
  if (!delta_schema) {
    return fail(error, TableResult::Invalid, "invalid schema: " + schema_error);
  }

  // Spark rejects these keys in TBLPROPERTIES; delta.* properties change the
  // protocol or how writers must behave, which LakeVilla does not implement
  static const std::set<std::string> reserved = {
      "comment", "location", "provider", "owner", "external",
      "is_managed_location"};
  std::set<std::string> keys;
  for (auto& property : options.properties) {
    auto key = lower(property.first);
    if (key.empty() || reserved.count(key) != 0 ||
        key.rfind("delta.", 0) == 0 || key.rfind("option.", 0) == 0) {
      return fail(error, TableResult::Invalid,
                  "unsupported table property '" + property.first + "'");
    }
    if (!keys.insert(property.first).second) {
      return fail(error, TableResult::Invalid,
                  "duplicate table property '" + property.first + "'");
    }
  }

  auto objects = this->connector_ptr->list_objects(*prefix);
  if (!objects) {
    return fail(error, TableResult::Error, "unable to list " + *prefix);
  }
  if (!objects->empty()) {
    std::string log_prefix = *prefix + "_delta_log/";
    bool is_table = std::any_of(
        objects->begin(), objects->end(), [&](const StorageConnector::ObjectInfo& object) {
          return object.key.rfind(log_prefix, 0) == 0;
        });
    return is_table ? fail(error, TableResult::AlreadyExists,
                           "a Delta table exists at " + *prefix)
                    : fail(error, TableResult::NotEmpty,
                           "the location " + *prefix + " is not empty");
  }

  // a global-log entry of an earlier table at this path would hide the new
  // table's versions from LV[Read]/LV[Hybrid]
  if (this->connector_ptr->check(this->global_log_path)) {
    if (!this->acquire_global_lock()) {
      return fail(error, TableResult::Error,
                  "unable to lock the global log " + this->global_log_path);
    }
    bool updated = this->update_global_entry(
        *prefix, [](std::optional<uint32_t>) { return std::nullopt; });
    this->release_global_lock();
    if (!updated) {
      return fail(error, TableResult::Error,
                  "unable to update the global log " + this->global_log_path);
    }
  }

  auto commit = create_table_commit(*delta_schema, options);
  std::string version0 = *prefix + "_delta_log/" + log_file_name(0, ".json");
  bool uncertain = false;
  for (uint32_t attempt = 0; attempt < this->storage_retries; attempt++) {
    auto status = this->connector_ptr->write_atomic(version0, commit.c_str(),
                                                    commit.size());
    if (status == StorageConnector::WriteStatus::Ok) {
      return TableResult::Ok;
    }
    if (status == StorageConnector::WriteStatus::AlreadyExists) {
      // an earlier attempt may have succeeded although its response was lost
      auto content = this->connector_ptr->read2(version0);
      if (uncertain && content->data &&
          std::string(content->data, content->length) == commit) {
        return TableResult::Ok;
      }
      return fail(error, TableResult::AlreadyExists,
                  "a Delta table was created concurrently at " + *prefix);
    }
    uncertain = true;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(LV_RETRY_BASE_MS << attempt));
  }
  return fail(error, TableResult::Error, "unable to write " + version0);
}

LHTransactions::TransactionManagerDeltaLake::TableResult
LHTransactions::TransactionManagerDeltaLake::drop_table(const std::string& path,
                                                        std::string* error) {
  auto prefix = table_prefix(path);
  if (!prefix) {
    return fail(error, TableResult::Invalid,
                "invalid table path '" + path + "'");
  }

  auto objects = this->connector_ptr->list_objects(*prefix);
  if (!objects) {
    return fail(error, TableResult::Error, "unable to list " + *prefix);
  }
  std::string log_prefix = *prefix + "_delta_log/";
  bool is_table = std::any_of(
      objects->begin(), objects->end(), [&](const StorageConnector::ObjectInfo& object) {
        return object.key.rfind(log_prefix, 0) == 0;
      });
  if (!is_table) {
    // never delete objects that do not belong to a Delta table
    return objects->empty()
               ? fail(error, TableResult::NotFound,
                      "no Delta table at " + *prefix)
               : fail(error, TableResult::NotEmpty,
                      "no Delta table at " + *prefix +
                          " (the location contains other objects)");
  }

  if (this->connector_ptr->check(this->global_log_path)) {
    if (!this->acquire_global_lock()) {
      return fail(error, TableResult::Error,
                  "unable to lock the global log " + this->global_log_path);
    }
    bool updated = this->update_global_entry(
        *prefix, [](std::optional<uint32_t>) { return std::nullopt; });
    this->release_global_lock();
    if (!updated) {
      return fail(error, TableResult::Error,
                  "unable to update the global log " + this->global_log_path);
    }
  }

  // first what a reader needs to reconstruct any version (_last_checkpoint,
  // checkpoints, version 0): afterwards the table cannot be read anymore;
  // then the data files, the rest of the log last (an interrupted drop stays
  // recognizable as a table and can be completed)
  std::vector<std::string> keys;
  for (auto& object : *objects) {
    keys.push_back(object.key);
  }
  std::string version0 = log_prefix + log_file_name(0, ".json");
  auto rank = [&](const std::string& key) {
    if (key.rfind(log_prefix, 0) != 0) {
      return 3;
    }
    if (key == log_prefix + "_last_checkpoint") {
      return 0;
    }
    if (key.find(".checkpoint.") != std::string::npos) {
      return 1;
    }
    if (key == version0) {
      return 2;
    }
    return 4;
  };
  std::stable_sort(keys.begin(), keys.end(),
                   [&](const std::string& a, const std::string& b) {
                     return rank(a) != rank(b) ? rank(a) < rank(b) : a < b;
                   });

  size_t failed = 0;
  for (auto& key : keys) {
    bool deleted = false;
    for (uint32_t attempt = 0; attempt < this->storage_retries && !deleted;
         attempt++) {
      deleted = this->connector_ptr->delete_file(key);
    }
    if (!deleted) {
      failed++;
    }
  }
  if (failed != 0) {
    return fail(error, TableResult::Error,
                "unable to delete " + std::to_string(failed) + " of " +
                    std::to_string(keys.size()) + " objects of " + *prefix);
  }
  return TableResult::Ok;
}
