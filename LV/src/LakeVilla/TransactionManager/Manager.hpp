#pragma once
#include <ctime>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>

#include "Connectors/MinIOConnector.hpp"
#include "Helpers/Helper.hpp"
#include "LakeVilla/DeltaLog/DeltaLogManager.hpp"
#include "LakeVilla/TransactionManager/Marker.hpp"
#include "settings.hpp"

#define NUM_RETRIES 5
#define WAIT_MILLISECONDS 60

namespace LHTransactions {
struct FileStats {
  std::string path;
  uint32_t size;
  std::vector<std::string> name;
  std::vector<std::string> max_val;
  std::vector<std::string> min_val;
  bool valid;
};

enum UpdateOperations { NONE, REP, ADD, SUB, MUL, DIV };

typedef std::vector<bool> LakeVillaConf;

struct TableStats {
  TableStats(std::string&& base_path);
  ~TableStats();

  uint32_t table_id;

  std::string base_path;
  std::shared_ptr<LHExecutor::LHDL::DeltaLogManager> delta_log;

  std::unique_ptr<Marker> marker;

  uint32_t in_bytes;
  uint32_t out_bytes;

  uint32_t file_num;

  uint32_t read_version;

  u_int32_t marker_version;

  bool prev_marker;

  std::unordered_map<std::string, std::unique_ptr<FileStats>> created_files;
  std::unordered_map<std::string, std::unique_ptr<FileStats>> deleted_files;

  bool opened;
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

  uint32_t next_step;

  bool explicit_log;

  uint32_t file_num;

  uint32_t transaction_id;

  uint32_t times_redo = 0;

  std::unique_ptr<StorageConnector::MinIOConnector> connector;

  StorageConnector::MinIOConnector* connector_ptr;

  std::unordered_map<std::string, std::unique_ptr<FileStats>> created_files;
  std::unordered_map<std::string, std::unique_ptr<FileStats>> deleted_files;

  TransactionManager(std::string& path_to_table,
                     StorageConnector::MinIOConfig config,
                     uint32_t transaction_id);

  TransactionManager(std::string& path_to_table,
                     StorageConnector::MinIOConnector* ptr,
                     uint32_t transaction_id);

  virtual ~TransactionManager() {};

  virtual bool begin_transaction() = 0;

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

  virtual bool read_file() = 0;

  virtual bool add_file() = 0;

  virtual bool add_file(uint32_t table_id, std::string& key, bool overwrite,
                        uint32_t prev = UINT32_MAX) = 0;

  virtual bool add_file(uint32_t table_id,
                        std::shared_ptr<arrow::Table> arrowTable,
                        const std::string& key, uint32_t prev = UINT32_MAX) = 0;

  virtual bool remove_file() = 0;

  virtual bool remove_file(uint32_t table_id, std::string& path,
                           uint32_t prev = UINT32_MAX) = 0;

  virtual bool remove_file(uint32_t table_id, const std::string& key,
                           std::string* path_ptr = nullptr,
                           uint32_t prev = UINT32_MAX) = 0;

  virtual bool commit(bool read_only = false, bool single_threaded = false) = 0;

  virtual bool commit(std::vector<double>& timestamps) = 0;

  virtual bool reroll() = 0;

  virtual bool redo(int atStep) = 0;

  virtual bool undo(int untilStep) = 0;

  virtual bool abort(bool clear_subdir = true) = 0;

  virtual uint32_t register_update_pairs(
      std::vector<std::pair<std::string, std::string>>& values) = 0;

  virtual uint32_t register_update_pairs(
      std::vector<std::pair<std::string, std::string>>& values,
      LHTransactions::UpdateOperations op,
      std::vector<std::pair<std::string, std::string>>& impl) = 0;

  virtual bool open_new_table(const std::string& path) = 0;

  virtual void print_stats() = 0;

  virtual uint32_t get_table_id(std::string& key) = 0;
};

};  // namespace LHTransactions