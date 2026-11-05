#include "DeltaLogManager.hpp"

#include <algorithm>

LHExecutor::LHDL::DeltaLogManager::DeltaLogManager(
    const std::string& base_path) {
  this->files.clear();
  this->old_files.clear();
  this->base_path = base_path;
  this->read_version = 0;
  this->write_version = UINT32_MAX;
}

LHExecutor::LHDL::DeltaLogManager::~DeltaLogManager() {
  this->files.clear();
  this->old_files.clear();
}

std::string LHExecutor::LHDL::DeltaLogManager::relative(const std::string& path,
                                                        bool use_base_path) {
  if (use_base_path && path.compare(0, this->base_path.size(),
                                    this->base_path) == 0) {
    return path.substr(this->base_path.size());
  }
  return path;
}

void LHExecutor::LHDL::DeltaLogManager::upsert(const std::string& path,
                                               DeltaLogEntry&& entry,
                                               DLOperation op, bool overwrite) {
  auto it = this->files.find(path);
  if (it != this->files.end()) {
    it->second.update(op, overwrite);
  } else {
    this->files.insert({path, std::move(entry)});
  }
}

bool LHExecutor::LHDL::DeltaLogManager::register_operation(
    LHExecutor::LHDL::DLOperation op, std::string& path, bool use_base_path,
    bool overwrite) {
  std::lock_guard<std::mutex> guard(this->mutex);
  this->upsert(this->relative(path, use_base_path), DeltaLogEntry(op), op,
               overwrite);
  return true;
}

bool LHExecutor::LHDL::DeltaLogManager::register_operation(
    LHExecutor::LHDL::DLOperation op, std::string& path,
    rapidjson::Document& stats, bool use_base_path, bool overwrite) {
  const char* stats_str = nullptr;
  int64_t size = -1;
  if (stats.IsObject() && stats.HasMember("add") && stats["add"].IsObject()) {
    auto& add = stats["add"];
    if (add.HasMember("stats") && add["stats"].IsString()) {
      stats_str = add["stats"].GetString();
    }
    if (add.HasMember("size") && add["size"].IsInt64()) {
      size = add["size"].GetInt64();
    }
  }

  std::lock_guard<std::mutex> guard(this->mutex);
  this->upsert(this->relative(path, use_base_path),
               DeltaLogEntry(op, stats_str, size), op, overwrite);
  return true;
}

bool LHExecutor::LHDL::DeltaLogManager::register_operation(
    LHExecutor::LHDL::DLOperation op, std::string& path,
    std::vector<std::string>& min, std::vector<std::string>& max,
    bool use_base_path, bool overwrite) {
  std::lock_guard<std::mutex> guard(this->mutex);
  this->upsert(this->relative(path, use_base_path), DeltaLogEntry(op, min, max),
               op, overwrite);
  return true;
}

bool LHExecutor::LHDL::DeltaLogManager::register_operation(
    LHExecutor::LHDL::DLOperation op, const std::string& path,
    DeltaLogEntry&& entry, bool use_base_path, bool overwrite) {
  std::lock_guard<std::mutex> guard(this->mutex);
  this->upsert(this->relative(path, use_base_path), std::move(entry), op,
               overwrite);
  return true;
}

std::vector<std::pair<std::string, LHExecutor::LHDL::DeltaLogManager::KeyMatch>>
LHExecutor::LHDL::DeltaLogManager::find_files(
    const std::string& column,
    const std::function<KeyMatch(const std::string*, const std::string*,
                                 int64_t)>& classify) {
  std::vector<std::pair<std::string, KeyMatch>> result;
  std::lock_guard<std::mutex> guard(this->mutex);
  for (auto& [path, entry] : this->files) {
    if (entry.operation != LHExecutor::LHDL::DLOperation::ADD) {
      continue;
    }
    auto match = classify(entry.min_of(column), entry.max_of(column),
                          entry.num_records);
    if (match != KeyMatch::None) {
      result.emplace_back(this->base_path + path, match);
    }
  }
  return result;
}

void LHExecutor::LHDL::DeltaLogManager::register_action(
    const std::string& path, DeltaLogEntry&& entry) {
  std::lock_guard<std::mutex> guard(this->mutex);
  auto it = this->files.find(path);
  if (it == this->files.end()) {
    this->files.insert({path, std::move(entry)});
    return;
  }

  auto& current = it->second;
  if (entry.version < current.version ||
      (entry.version == current.version &&
       (current.operation == DLOperation::DELETE ||
        entry.operation != DLOperation::DELETE))) {
    return;
  }
  // a remove without extended file metadata keeps that of the add
  if (entry.operation == DLOperation::DELETE && entry.size < 0 &&
      current.operation == DLOperation::ADD) {
    entry.size = current.size;
    entry.partition_values = current.partition_values;
  }
  current = std::move(entry);
}

void LHExecutor::LHDL::DeltaLogManager::set_protocol(const std::string& json,
                                                     uint32_t version) {
  std::lock_guard<std::mutex> guard(this->mutex);
  if (static_cast<int64_t>(version) >= this->protocol_version) {
    this->protocol = json;
    this->protocol_version = version;
  }
}

void LHExecutor::LHDL::DeltaLogManager::set_metadata(const std::string& json,
                                                     uint32_t version) {
  std::lock_guard<std::mutex> guard(this->mutex);
  if (static_cast<int64_t>(version) >= this->metadata_version) {
    this->metadata = json;
    this->metadata_version = version;
  }
}

std::string LHExecutor::LHDL::DeltaLogManager::get_protocol() {
  std::lock_guard<std::mutex> guard(this->mutex);
  return this->protocol;
}

std::string LHExecutor::LHDL::DeltaLogManager::get_metadata() {
  std::lock_guard<std::mutex> guard(this->mutex);
  return this->metadata;
}

int64_t LHExecutor::LHDL::DeltaLogManager::get_metadata_version() {
  std::lock_guard<std::mutex> guard(this->mutex);
  return this->metadata_version;
}

std::vector<std::pair<std::string, std::optional<std::string>>>
LHExecutor::LHDL::DeltaLogManager::get_partition_values(const std::string& path,
                                                        bool use_base_path) {
  std::string json;
  {
    std::lock_guard<std::mutex> guard(this->mutex);
    auto it = this->files.find(this->relative(path, use_base_path));
    if (it == this->files.end()) {
      return {};
    }
    json = it->second.partition_values;
  }
  std::vector<std::pair<std::string, std::optional<std::string>>> values;
  rapidjson::Document doc;
  doc.Parse(json.c_str(), json.size());
  if (doc.HasParseError() || !doc.IsObject()) {
    return values;
  }
  for (auto& member : doc.GetObject()) {
    if (member.value.IsString()) {
      values.emplace_back(member.name.GetString(), member.value.GetString());
    } else {
      values.emplace_back(member.name.GetString(), std::nullopt);
    }
  }
  return values;
}

void LHExecutor::LHDL::DeltaLogManager::mark_unsupported(
    const std::string& reason) {
  std::lock_guard<std::mutex> guard(this->mutex);
  if (this->unsupported.empty()) {
    this->unsupported = reason;
  }
}

std::string LHExecutor::LHDL::DeltaLogManager::get_unsupported() {
  std::lock_guard<std::mutex> guard(this->mutex);
  return this->unsupported;
}

void LHExecutor::LHDL::DeltaLogManager::set_transaction(
    const std::string& app_id, int64_t txn_version, int64_t last_updated,
    uint32_t version) {
  std::lock_guard<std::mutex> guard(this->mutex);
  auto it = this->transactions.find(app_id);
  if (it != this->transactions.end() && it->second.version > version) {
    return;
  }
  this->transactions[app_id] = {{app_id, txn_version, last_updated}, version};
}

void LHExecutor::LHDL::DeltaLogManager::set_domain_metadata(
    const std::string& domain, std::optional<std::string> configuration,
    uint32_t version) {
  std::lock_guard<std::mutex> guard(this->mutex);
  auto it = this->domains.find(domain);
  if (it != this->domains.end() && it->second.version > version) {
    return;
  }
  this->domains[domain] = {std::move(configuration), version};
}

std::vector<LHHelpers::CheckpointTxn>
LHExecutor::LHDL::DeltaLogManager::get_transactions() {
  std::vector<LHHelpers::CheckpointTxn> result;
  std::lock_guard<std::mutex> guard(this->mutex);
  for (auto& entry : this->transactions) {
    result.push_back(entry.second.txn);
  }
  return result;
}

std::vector<LHHelpers::CheckpointDomain>
LHExecutor::LHDL::DeltaLogManager::get_domain_metadata() {
  std::vector<LHHelpers::CheckpointDomain> result;
  std::lock_guard<std::mutex> guard(this->mutex);
  for (auto& entry : this->domains) {
    if (entry.second.configuration) {
      result.push_back({entry.first, *entry.second.configuration});
    }
  }
  return result;
}

std::vector<std::pair<std::string, LHExecutor::LHDL::DeltaLogEntry>>
LHExecutor::LHDL::DeltaLogManager::get_tombstones() {
  std::vector<std::pair<std::string, DeltaLogEntry>> result;

  std::lock_guard<std::mutex> guard(this->mutex);
  for (auto& comp : this->files) {
    if (comp.second.operation == LHExecutor::LHDL::DLOperation::DELETE) {
      result.emplace_back(comp.first, comp.second);
    }
  }
  return result;
}

std::vector<std::string> LHExecutor::LHDL::DeltaLogManager::get_list() {
  std::vector<std::string> result;

  std::lock_guard<std::mutex> guard(this->mutex);
  for (auto& comp : this->files) {
    if (comp.second.operation == LHExecutor::LHDL::DLOperation::ADD) {
      result.push_back(this->base_path + comp.first);
    }
  }

  return result;
}

std::vector<std::string> LHExecutor::LHDL::DeltaLogManager::get_list_raw() {
  std::vector<std::string> result;

  std::lock_guard<std::mutex> guard(this->mutex);
  for (auto& comp : this->files) {
    if (comp.second.operation == LHExecutor::LHDL::DLOperation::ADD) {
      result.push_back(comp.first);
    }
  }

  return result;
}

std::vector<std::pair<std::string, LHExecutor::LHDL::DeltaLogEntry>>
LHExecutor::LHDL::DeltaLogManager::get_entries() {
  std::vector<std::pair<std::string, DeltaLogEntry>> result;

  std::lock_guard<std::mutex> guard(this->mutex);
  for (auto& comp : this->files) {
    if (comp.second.operation == LHExecutor::LHDL::DLOperation::ADD) {
      result.emplace_back(comp.first, comp.second);
    }
  }

  return result;
}

int64_t LHExecutor::LHDL::DeltaLogManager::get_size(const std::string& path,
                                                    bool use_base_path) {
  std::lock_guard<std::mutex> guard(this->mutex);
  auto it = this->files.find(this->relative(path, use_base_path));
  if (it == this->files.end()) {
    return -1;
  }
  return it->second.size;
}

std::string LHExecutor::LHDL::DeltaLogManager::findFileWithStats(
    uint32_t pos, const std::string& key, bool useBase) {
  std::string name = "";

  {
    std::lock_guard<std::mutex> guard(this->mutex);
    for (auto& comp : this->files) {
      auto& entry = comp.second;
      if (entry.operation != LHExecutor::LHDL::DLOperation::ADD ||
          pos >= entry.min.size() || pos >= entry.max.size()) {
        continue;
      }
      if (entry.min[pos].compare(key) == 0 ||
          entry.max[pos].compare(key) == 0) {
        name = comp.first;
        break;
      }
    }
  }

  if (useBase && !name.empty()) {
    return this->base_path + name;
  }
  return name;
}

bool LHExecutor::LHDL::DeltaLogManager::register_read_version(
    uint32_t version) {
  std::lock_guard<std::mutex> guard(this->mutex);
  if (version > this->read_version) {
    this->read_version = version;
  }
  return true;
}

bool LHExecutor::LHDL::DeltaLogManager::register_write_version(
    uint32_t version) {
  std::lock_guard<std::mutex> guard(this->mutex);
  if (this->read_version < version) {
    this->write_version = version;
    return true;
  }
  return false;
}

uint32_t LHExecutor::LHDL::DeltaLogManager::get_write_version() {
  std::lock_guard<std::mutex> guard(this->mutex);
  return this->write_version;
}

uint32_t LHExecutor::LHDL::DeltaLogManager::get_read_version() {
  std::lock_guard<std::mutex> guard(this->mutex);
  return this->read_version;
}

void LHExecutor::LHDL::DeltaLogManager::ToString() {
  auto list = this->get_list();
  for (auto& ref : list) {
    std::cout << ref << std::endl;
  }
}
