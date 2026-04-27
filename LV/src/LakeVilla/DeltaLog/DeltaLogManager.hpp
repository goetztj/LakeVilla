#pragma once
#include <atomic>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "DeltaLogEntry.hpp"

namespace LHExecutor::LHDL {

struct DeltaLogManager {
  DeltaLogManager(const std::string& base_path);
  ~DeltaLogManager();

  bool register_operation(DLOperation op, std::string& path,
                          bool use_base_path = false, bool overwrite = false);
  bool register_operation(DLOperation op, std::string& path,
                          rapidjson::Document& stats,
                          bool use_base_path = false, bool overwrite = false);
  bool register_operation(DLOperation op, std::string &path,
                          std::vector<std::string> &min,
                          std::vector<std::string> &max,
                          bool use_base_path = false, bool overwrite = false);
                          
  std::vector<std::string> get_list();

  std::vector<std::string> get_list_raw();

  std::string findFileWithStats(uint32_t pos, const std::string &key,
                                bool useBase = true);

  void ToString();

  bool register_write_version(uint32_t version);

  uint32_t get_write_version();
  uint32_t get_read_version();

  bool register_read_version(uint32_t version);

 private:
  std::string base_path;

  std::unordered_map<std::string, DeltaLogEntry> files;
  std::unordered_map<std::string, DeltaLogEntry> old_files;

  std::atomic_bool in_use;

  uint32_t read_version;
  uint32_t write_version;
};

};  // namespace LHExecutor::LHDL