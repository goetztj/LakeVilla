#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "DeltaLogEntry.hpp"

namespace LHExecutor::LHDL {

struct DeltaLogManager {
  DeltaLogManager(const std::string& base_path);
  ~DeltaLogManager();

  bool register_operation(DLOperation op, std::string& path,
                          bool use_base_path = false, bool overwrite = false);
  // registers an add/remove action of the Delta log (stats are optional)
  bool register_operation(DLOperation op, std::string& path,
                          rapidjson::Document& stats,
                          bool use_base_path = false, bool overwrite = false);
  bool register_operation(DLOperation op, std::string &path,
                          std::vector<std::string> &min,
                          std::vector<std::string> &max,
                          bool use_base_path = false, bool overwrite = false);

  // registers a file with its entry (e.g. the statistics of a file the
  // transaction wrote)
  bool register_operation(DLOperation op, const std::string& path,
                          DeltaLogEntry&& entry, bool use_base_path,
                          bool overwrite);

  enum class KeyMatch { None, Possible, Exact };
  // added files whose statistics of a column may contain a key: classify()
  // decides from the column's min and max (nullptr if the statistics do not
  // contain the column) and numRecords (-1 if unknown); paths include the
  // base path
  std::vector<std::pair<std::string, KeyMatch>> find_files(
      const std::string& column,
      const std::function<KeyMatch(const std::string*, const std::string*,
                                   int64_t)>& classify);

  // registers an add (ADD) or remove (DELETE) action of the log or of a
  // checkpoint (entry.version: its version); logs may be parsed in any
  // order: the action of the highest version wins, and a remove wins over
  // an add of the same version
  void register_action(const std::string& path, DeltaLogEntry&& entry);

  // latest protocol/metaData action (JSON of the action's value)
  void set_protocol(const std::string& json, uint32_t version);
  void set_metadata(const std::string& json, uint32_t version);
  std::string get_protocol();
  std::string get_metadata();
  // log version of the latest metaData action (-1: none)
  int64_t get_metadata_version();

  // partitionValues of the add action of a file (column, value; std::nullopt
  // = null); empty for unknown files and unpartitioned tables
  std::vector<std::pair<std::string, std::optional<std::string>>>
  get_partition_values(const std::string& path, bool use_base_path = true);

  // latest txn action per application and domainMetadata action per domain
  // (configuration std::nullopt: the domain was removed)
  void set_transaction(const std::string& app_id, int64_t txn_version,
                       int64_t last_updated, uint32_t version);
  void set_domain_metadata(const std::string& domain,
                           std::optional<std::string> configuration,
                           uint32_t version);
  std::vector<LHHelpers::CheckpointTxn> get_transactions();
  // domains that were not removed
  std::vector<LHHelpers::CheckpointDomain> get_domain_metadata();

  // the table uses Delta features LakeVilla does not track (e.g. txn
  // actions, deletion vectors); checkpoints written by LakeVilla would be
  // incomplete
  void mark_unsupported(const std::string& reason);
  std::string get_unsupported();

  // removed files (tombstones) and their remove actions
  std::vector<std::pair<std::string, DeltaLogEntry>> get_tombstones();

  std::vector<std::string> get_list();

  std::vector<std::string> get_list_raw();

  // all currently added files (paths relative to the table) and their entry
  std::vector<std::pair<std::string, DeltaLogEntry>> get_entries();

  // size of an added file as recorded in the log (-1 if unknown)
  int64_t get_size(const std::string& path, bool use_base_path = false);

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

  std::mutex mutex;

  uint32_t read_version;
  uint32_t write_version;

  std::string protocol;
  int64_t protocol_version = -1;
  std::string metadata;
  int64_t metadata_version = -1;
  std::string unsupported;

  // with the log version of the action
  struct TransactionState {
    LHHelpers::CheckpointTxn txn;
    uint32_t version;
  };
  struct DomainState {
    std::optional<std::string> configuration;
    uint32_t version;
  };
  // by appId / domain
  std::unordered_map<std::string, TransactionState> transactions;
  std::unordered_map<std::string, DomainState> domains;

  std::string relative(const std::string& path, bool use_base_path);

  // inserts or updates an entry; mutex must be held
  void upsert(const std::string& path, DeltaLogEntry&& entry, DLOperation op,
              bool overwrite);
};

};  // namespace LHExecutor::LHDL
