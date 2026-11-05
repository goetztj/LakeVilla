#pragma once
#include <atomic>
#include <ctime>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Connectors/Connector.hpp"
#include "Connectors/MinIOConfig.hpp"
#include "Helpers/DeltaSchema.hpp"
#include "Helpers/Helper.hpp"
#include "LakeVilla/DeltaLog/DeltaLogManager.hpp"
#include "LakeVilla/TransactionManager/Marker.hpp"
#include "LakeVilla/TransactionManager/ProtocolOptions.hpp"

#define NUM_RETRIES 5
#define WAIT_MILLISECONDS 60

// default locations of the global version log (level 2)
#define LV_DEFAULT_GLOBAL_LOG "wh/_global/log.txt"
#define LV_DEFAULT_GLOBAL_TMP_LOG "wh/_global/log.txt.tmp"

namespace LHTransactions {

// default global-log paths of new managers (e.g. from lvconfig.conf)
void set_default_global_log_paths(const std::string& log_path,
                                  const std::string& tmp_log_path);
std::pair<std::string, std::string> default_global_log_paths();

// a transaction id that is unique within the process and (with high
// probability) across processes; ids name data files and identify markers
uint32_t unique_transaction_id();

// a file added or removed by the transaction
struct FileStats {
  // full path (including the table's base path)
  std::string path;
  // size in bytes (-1 if unknown)
  int64_t size = -1;
  // statistics written to the Delta log (numRecords -1 = unknown)
  LHHelpers::TableStatistics stats;
  bool valid = true;
  // the update (register_update_pairs id) whose new rows the file holds;
  // removes of that update by key do not remove it
  uint32_t update_id = UINT32_MAX;

  // min/max values of the columns with statistics (local lookups by key)
  std::vector<std::string> min_values() const;
  std::vector<std::string> max_values() const;
};

enum UpdateOperations { NONE, REP, ADD, SUB, MUL, DIV };

typedef std::vector<bool> LakeVillaConf;

struct TableStats {
  TableStats(std::string&& base_path);
  ~TableStats();

  uint32_t table_id = 0;

  std::string base_path;
  std::shared_ptr<LHExecutor::LHDL::DeltaLogManager> delta_log;

  std::unique_ptr<Marker> marker;

  uint32_t in_bytes = 0;
  uint32_t out_bytes = 0;

  uint32_t file_num = 0;

  uint32_t read_version = 0;

  uint32_t marker_version = 0;

  bool prev_marker = false;

  std::unordered_map<std::string, std::unique_ptr<FileStats>> created_files;
  std::unordered_map<std::string, std::unique_ptr<FileStats>> deleted_files;

  // the transaction's commit for this table was written
  bool committed = false;

  // the transaction read the table (validated for serializability)
  bool was_read = false;

  // a concurrent commit changed the table's data (add/remove actions)
  bool concurrent_change = false;

  // files (relative paths) removed by commits of concurrent transactions
  // that were read while waiting for their markers
  std::unordered_set<std::string> concurrent_removes;

  bool opened;

  // logical schema of the snapshot (metaData) that data files are conformed
  // to; parsed again when another metaData action is read (schema_source)
  std::shared_ptr<const LHHelpers::TableSchema> schema;
  std::pair<const void*, int64_t> schema_source = {nullptr, -2};
  std::string schema_error;
};

// manages the global relations of a table
struct GlobalRelation {
  GlobalRelation();

  ~GlobalRelation();

  bool register_precedence(uint32_t txn_id);

  bool register_dependency(uint32_t txn_id);

  std::vector<uint32_t> get_dependencies();

 private:
  std::unordered_map<uint32_t, uint32_t> precedence;

  std::unordered_map<uint32_t, uint32_t> dependency;
};

struct TransactionManager {
  enum SubOpType { READ, ADD, DELETE };

  std::string base_path;

  std::vector<std::unique_ptr<TableStats>> tables;

  std::string working_dir;

  uint32_t next_step = 0;

  std::atomic<bool> explicit_log;

  uint32_t file_num = 0;

  uint32_t transaction_id;

  uint32_t times_redo = 0;

  // owned connector (only set if the manager created it itself)
  std::unique_ptr<StorageConnector::Connector> connector;

  StorageConnector::Connector* connector_ptr;

  // global version log (level 2) and its lock file
  std::string global_log_path = default_global_log_paths().first;
  std::string global_tmp_log_path = default_global_log_paths().second;

  // protocol settings (isolation, marker leases, global-log repair)
  ProtocolOptions options = default_protocol_options();

  std::unordered_map<std::string, std::unique_ptr<FileStats>> created_files;
  std::unordered_map<std::string, std::unique_ptr<FileStats>> deleted_files;

  // creates and owns an S3 connector (defined in lakevilla-s3)
  TransactionManager(std::string& path_to_table,
                     StorageConnector::MinIOConfig config,
                     uint32_t transaction_id);

  TransactionManager(std::string& path_to_table,
                     StorageConnector::Connector* ptr,
                     uint32_t transaction_id);

  void set_protocol_options(const ProtocolOptions& options) {
    this->options = options;
  }

  void set_global_log_paths(const std::string& log_path,
                            const std::string& tmp_log_path) {
    this->global_log_path = log_path;
    this->global_tmp_log_path = tmp_log_path;
  }

  virtual ~TransactionManager() {};

  // unused: interactive transaction
  // virtual bool begin_transaction() = 0;

  virtual bool begin_transaction_ycsb() = 0;

  virtual std::shared_ptr<arrow::Table> read_table(
      uint32_t table_id, uint32_t num_threads = 8) = 0;

  virtual std::shared_ptr<arrow::Table> read_random_file_as_table(
      uint32_t table_id, std::string& path) = 0;

  virtual std::shared_ptr<arrow::Table> read_file_as_table(
      uint32_t table_id, const std::string& key, std::string& path) = 0;

  virtual std::string read_file(
      uint32_t table_id, const std::string& key,
      const std::vector<std::string>* fields,
      std::vector<std::pair<std::string, std::string>>& result,
      std::string& path) = 0;

  // unused: interactive read
  // virtual bool read_file() = 0;

  // unused: interactive add
  // virtual bool add_file() = 0;

  virtual bool add_file(uint32_t table_id, std::string& key, bool overwrite,
                        uint32_t prev = UINT32_MAX) = 0;

  virtual bool add_file(uint32_t table_id,
                        std::shared_ptr<arrow::Table> arrowTable,
                        const std::string& key, uint32_t prev = UINT32_MAX) = 0;

  // unused: interactive remove
  // virtual bool remove_file() = 0;

  virtual bool remove_file(uint32_t table_id, std::string& path,
                           uint32_t prev = UINT32_MAX) = 0;

  virtual bool remove_file(uint32_t table_id, const std::string& key,
                           std::string* path_ptr = nullptr,
                           uint32_t prev = UINT32_MAX) = 0;

  virtual bool commit(bool read_only = false, bool single_threaded = false) = 0;

  // unused
  // virtual bool commit(std::vector<double>& timestamps) = 0;

  virtual bool reroll() = 0;

  virtual bool redo(int atStep) = 0;

  // unused
  // virtual bool undo(int untilStep) = 0;

  virtual bool abort(bool clear_subdir = true) = 0;

  virtual uint32_t register_update_pairs(
      std::vector<std::pair<std::string, std::string>>& values) = 0;

  virtual uint32_t register_update_pairs(
      std::vector<std::pair<std::string, std::string>>& values,
      LHTransactions::UpdateOperations op,
      std::vector<std::pair<std::string, std::string>>& impl) = 0;

  virtual bool open_new_table(const std::string& path) = 0;

  // unused
  // virtual void print_stats() = 0;

  virtual uint32_t get_table_id(std::string& key) = 0;
};

};  // namespace LHTransactions