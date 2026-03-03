#include "DeltaLogManager.hpp"

LHExecutor::LHDL::DeltaLogManager::DeltaLogManager(
    const std::string& base_path) {
  this->files.clear();
  this->old_files.clear();
  this->in_use.store(false);
  this->base_path = base_path;
  this->read_version = 0;
  this->write_version = -1;
}

LHExecutor::LHDL::DeltaLogManager::~DeltaLogManager() {
  this->files.clear();
  this->old_files.clear();
}

bool LHExecutor::LHDL::DeltaLogManager::register_operation(
    LHExecutor::LHDL::DLOperation op, std::string& path, bool use_base_path,
    bool overwrite) {
  while (this->in_use.exchange(true)) {
  }

  std::string comp_path;
  if (use_base_path) {
    comp_path = path.substr(this->base_path.size(), path.size());
  } else {
    comp_path = path;
  }

  if (this->files.count(comp_path) != 0) {
    this->files.find(comp_path)->second.update(op, overwrite);
  } else {
    LHExecutor::LHDL::DeltaLogEntry entry(op);

    this->files.insert({comp_path, std::move(entry)});
  }

  this->in_use.store(false);
  return true;
}

bool LHExecutor::LHDL::DeltaLogManager::register_operation(
    LHExecutor::LHDL::DLOperation op, std::string& path,
    rapidjson::Document& stats, bool use_base_path, bool overwrite) {
  while (this->in_use.exchange(true)) {
  }

  std::string comp_path;
  if (use_base_path) {
    comp_path = path.substr(this->base_path.size(), path.size());
  } else {
    comp_path = path;
  }

  if (this->files.count(comp_path) != 0) {
    this->files.find(comp_path)->second.update(op, overwrite);
  } else {
    auto sub_json = stats["add"]["stats"].GetString();
    LHExecutor::LHDL::DeltaLogEntry entry(op, sub_json);

    this->files.insert({comp_path, std::move(entry)});
  }

  this->in_use.store(false);
  return true;
}

bool LHExecutor::LHDL::DeltaLogManager::register_operation(
    LHExecutor::LHDL::DLOperation op, std::string& path,
    std::vector<std::string>& min, std::vector<std::string>& max,
    bool use_base_path, bool overwrite) {
  while (this->in_use.exchange(true)) {
  }

  std::string comp_path;
  if (use_base_path) {
    comp_path = path.substr(this->base_path.size(), path.size());
  } else {
    comp_path = path;
  }

  if (this->files.count(comp_path) != 0) {
    this->files.find(comp_path)->second.update(op, overwrite);
  } else {
    LHExecutor::LHDL::DeltaLogEntry entry(op, min, max);

    this->files.insert({comp_path, std::move(entry)});
  }

  this->in_use.store(false);
  return true;
}

std::vector<std::string> LHExecutor::LHDL::DeltaLogManager::get_list() {
  std::vector<std::string> result;

  while (this->in_use.exchange(true)) {
  }

  for (auto& comp : this->files) {
    if (comp.second.operation == LHExecutor::LHDL::DLOperation::ADD) {
      result.push_back(this->base_path + comp.first);
    }
  }

  this->in_use.store(false);
  return result;
}

std::vector<std::string> LHExecutor::LHDL::DeltaLogManager::get_list_raw() {
  std::vector<std::string> result;

  while (this->in_use.exchange(true)) {
  }

  for (auto& comp : this->files) {
    if (comp.second.operation == LHExecutor::LHDL::DLOperation::ADD) {
      result.push_back(comp.first);
    }
  }

  this->in_use.store(false);
  return result;
}

std::string LHExecutor::LHDL::DeltaLogManager::findFileWithStats(
    uint32_t pos, const std::string& key, bool useBase) {
  while (this->in_use.exchange(true)) {
  }

  std::string name = "";

  for (auto& comp : this->files) {
    if (comp.second.operation == LHExecutor::LHDL::DLOperation::ADD) {
      if (comp.second.min[pos].compare(key) == 0 ||
          comp.second.max[pos].compare(key) == 0) {
        name = comp.first;
        break;
      }
    }
  }

  this->in_use.store(false);
  if (useBase && !name.empty()) {
    return this->base_path + name;
  }
  return name;
}

bool LHExecutor::LHDL::DeltaLogManager::register_read_version(
    uint32_t version) {
  if (version > this->read_version) {
    this->read_version = version;
  }
  return true;
}

bool LHExecutor::LHDL::DeltaLogManager::register_write_version(
    uint32_t version) {
  if (this->read_version < version) {
    this->write_version = version;
    return true;
  }
  return false;
}

uint32_t LHExecutor::LHDL::DeltaLogManager::get_write_version() {
  return this->write_version;
}

uint32_t LHExecutor::LHDL::DeltaLogManager::get_read_version() {
  return this->read_version;
}

void LHExecutor::LHDL::DeltaLogManager::ToString() {
  auto list = this->get_list();
  for (auto& ref : list) {
    std::cout << ref << std::endl;
  }
}