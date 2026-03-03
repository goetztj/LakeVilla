#pragma once
#include <unistd.h>

#include <ctime>
#include <memory>
#include <stack>
#include <string>

#include "LakeVilla/TransactionManager/Manager.hpp"

#define NUM_RETRIES 5
#define WAIT_MILLISECONDS 60

namespace LHTransactions {

struct TransactionManagerGeneric : public TransactionManager {
  std::vector<std::pair<SubOpType, std::string>> ops;

  LakeVillaConf levels;

  TransactionManagerGeneric(LakeVillaConf levels, std::string& path_to_table,
                            StorageConnector::MinIOConfig config,
                            uint32_t transaction_id);

  TransactionManagerGeneric(LakeVillaConf levels, std::string& path_to_table,
                            StorageConnector::MinIOConnector* conn_ptr,
                            uint32_t transaction_id);

  ~TransactionManagerGeneric();

  bool begin_transaction() override;

  bool begin_transaction_ycsb() override;

  std::shared_ptr<arrow::Table> read_table(uint32_t table_id,
                                           uint32_t num_threads = 2) override;

  void read_table_simple(uint32_t table_id, uint32_t num_threads = 2);
  void read_partial_table_simple(uint32_t table_id, uint32_t skip,
                                 uint32_t num_threads = 2);

  void head_table_simple(uint32_t table_id, uint32_t num_threads = 2);

  std::shared_ptr<arrow::Table> read_file_as_table(uint32_t table_id,
                                                   const std::string& key,
                                                   std::string& path) override;

  std::shared_ptr<arrow::Table> read_random_file_as_table(
      uint32_t table_id, std::string& path) override;

  void read_random_file_simple(uint32_t table_id, std::string& path);

  std::string read_file(
      uint32_t table_id, const std::string& key,
      const std::vector<std::string>* fields,
      std::vector<std::pair<std::string, std::string>>& result,
      std::string& path) override;

  bool read_file() override;

  bool add_file() override;

  bool add_file(uint32_t table_id, std::string& path, bool overwrite,
                uint32_t prev = UINT32_MAX) override;

  bool add_file(uint32_t table_id, std::shared_ptr<arrow::Table> arrowTable,
                const std::string& key, uint32_t prev = UINT32_MAX) override;

  bool add_file(uint32_t table_id, std::string& content, const std::string& key,
                uint32_t prev = UINT32_MAX);

  bool remove_file() override;

  bool remove_file(uint32_t table_id, std::string& path,
                   uint32_t prev = UINT32_MAX) override;

  bool remove_file(uint32_t table_id, const std::string& key,
                   std::string* path_ptr = nullptr,
                   uint32_t prev = UINT32_MAX) override;

  bool commit(bool read_only = false, bool single_threaded = false) override;

  bool commit(std::vector<double>& timestamps) override;

  bool commit(double& data_read, double& data_write,
              bool single_threaded = false);

  bool reroll() override;

  bool redo(int atStep) override;

  bool undo(int untilStep) override;

  bool abort(bool clear_subdir = true) override;

  uint32_t register_update_pairs(
      std::vector<std::pair<std::string, std::string>>& values) override;

  uint32_t register_update_pairs(
      std::vector<std::pair<std::string, std::string>>& values,
      LHTransactions::UpdateOperations op,
      std::vector<std::pair<std::string, std::string>>& impl) override;

  bool open_new_table(const std::string& path) override;

  void print_stats() override;

  uint32_t get_table_id(std::string& key) override;

  void create_checkpoint(uint32_t tbl_id);

 private:
  bool cleared = false;
  int times_redo_num = 0;
  double data_read;
  double data_write;
  std::unique_ptr<LHTransactions::GlobalRelation> relation;

  std::stack<std::pair<std::string, std::string>> old_found;

  std::vector<std::vector<std::pair<std::string, std::string>>> update_vals;

  std::vector<LHTransactions::UpdateOperations> update_ops;

  std::vector<std::string> prev_markers;

  std::vector<double> commit_diff;

  std::pair<std::string, std::string> found;

  std::unordered_map<std::string, uint32_t> local_log;

  bool global_log_exists;

  std::string big_cache;

  bool begin(std::string base_path);

  void parse_checkpoint(LHTransactions::TableStats* stats,
                        uint32_t checkpoint_nr);

  void parse_logentries(uint32_t start, uint32_t end,
                        Aws::Vector<Aws::S3::Model::Object>& list,
                        LHTransactions::TableStats* stats, uint32_t max_version,
                        bool& dep);

  void parse_logentries2(uint32_t start, uint32_t end,
                         LHTransactions::TableStats* stats,
                         uint32_t max_version, bool& dep);

  std::unique_ptr<TableStats> open_tableV2(std::string&& table_path,
                                           uint32_t num_threads = 2);

  bool reopen_tables(uint32_t num_threads = 2);

  bool wait_for_marker(std::string& path);

  std::unique_ptr<TableStats> open_table(std::string&& table_path);

  bool open_new_table();

  TableStats* select_table();

  // registeres information containied in DL logs; checks for markers
  // returns: true if the json contained a DL entry; false if the entry was an
  // active marker
  uint32_t parseEntry(rapidjson::Document& val,
                      LHExecutor::LHDL::DeltaLogManager* manager);

  std::pair<std::string, LHExecutor::LHDL::DLOperation> parseEntry(
      rapidjson::Document& val);

  void createHeaderMarker(std::string& out_string);

  void createMarkerEntry(std::string& working_dir, std::string& out_str);

  bool checkMarker(const rapidjson::Document* content);

  void createHeaderCommit(std::string& out_string, TableStats& stats);

  void createOperationDeleteEntry(FileStats& file, std::string& out_str);

  void createOperationAddEntry(FileStats& file, std::string& out_str);

  bool add_substep_implicit(std::string& file, SubOpType type,
                            TableStats& table, const std::string* key);

  bool add_substep_explicit(std::string& file, SubOpType type,
                            TableStats& table, const std::string* key);

  bool add_substep(std::string& file, SubOpType type, TableStats& table,
                   const std::string* key);

  void clear_subdirectory();

  bool execute_reroll_step(std::string exec_string);

  bool parse_global_snapshots();

  bool start_validation(
      std::unordered_map<std::string, uint32_t>& version_read_map,
      std::unordered_map<std::string, uint32_t>& version_write_map,
      std::string& content);

  bool create_global_snapshot(
      std::unordered_map<std::string, uint32_t>& version_read_map,
      std::unordered_map<std::string, uint32_t>& version_write_map,
      std::string& content);

  // reads files in list between [start, end[
  void read_part(std::shared_ptr<arrow::Table>& table_ptr,
                 std::vector<std::string>& list, uint32_t start, uint32_t end);

  // reads files in list between [start, end[
  void read_part_simple(std::vector<std::string>& list, uint32_t start,
                        uint32_t end);

  // reads files in list between [start, end[
  void read_part_simple_skip(std::vector<std::string>& list, uint32_t start,
                             uint32_t end, uint32_t skip);

  // head files in list between [start, end[
  void head_part_simple(std::vector<std::string>& list, uint32_t start,
                        uint32_t end);

  std::shared_ptr<arrow::Array> build_string_col(
      std::string& name, std::shared_ptr<arrow::ChunkedArray>& old_col,
      LHTransactions::UpdateOperations op, std::string& val);

  std::shared_ptr<arrow::Array> build_int_col(
      std::string& name, std::shared_ptr<arrow::ChunkedArray>& old_col,
      LHTransactions::UpdateOperations op, int val);
};

};  // namespace LHTransactions