#pragma once
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iomanip>
#include <mutex>
#include <optional>
#include <ctime>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stack>
#include <string>
#include <thread>
#include <vector>

#include "LakeVilla/TransactionManager/Manager.hpp"

// bounded retries for storage operations: LV_RETRY_BASE_MS * 2^attempt
#define LV_STORAGE_RETRIES 8
#define LV_RETRY_BASE_MS 10

namespace LHTransactions {

// name of a Delta log file, e.g. log_file_name(3, ".json") ->
// 00000000000000000003.json
std::string log_file_name(uint64_t version, const std::string& suffix);

// version of a Delta log commit file (<path>/_delta_log/<20 digits>.json)
std::optional<uint64_t> parse_log_version(const std::string& key);

// a classic Delta checkpoint (single file or multi-part)
struct CheckpointInfo {
  std::optional<uint64_t> version;
  uint32_t parts = 1;
};

// the newest complete classic checkpoint among the objects of a Delta log
// with a version <= max_version (multi-part checkpoints need all parts)
CheckpointInfo newest_checkpoint(
    const std::vector<StorageConnector::ObjectInfo>& objects,
    uint64_t max_version);

// Delta checkpoint interval of a table (metaData JSON): the table property
// delta.checkpointInterval, default 10
uint32_t checkpoint_interval(const std::string& metadata);

// retention of tombstones in checkpoints in ms (metaData JSON): the table
// property delta.deletedFileRetentionDuration, default one week
int64_t tombstone_retention_ms(const std::string& metadata);

// "" if LakeVilla can write a checkpoint for a table with this protocol
// (JSON of the protocol action), otherwise the reason
std::string checkpoint_blocker(const std::string& protocol);

// splits [begin, end[ into at most 'parts' non-empty ranges
std::vector<std::pair<uint32_t, uint32_t>> split_range(uint32_t begin,
                                                       uint32_t end,
                                                       uint32_t parts);

struct TransactionManagerDeltaLake : public TransactionManager {
  // unused (written, never read)
  // std::vector<std::pair<SubOpType, std::string>> ops;

  LakeVillaConf levels;

  TransactionManagerDeltaLake(LakeVillaConf levels, std::string& path_to_table,
                              StorageConnector::MinIOConfig config,
                              uint32_t transaction_id);

  TransactionManagerDeltaLake(LakeVillaConf levels, std::string& path_to_table,
                              StorageConnector::Connector* conn_ptr,
                              uint32_t transaction_id);

  ~TransactionManagerDeltaLake();

  // unused: interactive transaction
  // bool begin_transaction() override;

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

  // unused: interactive read
  // bool read_file() override;

  // unused: interactive add
  // bool add_file() override;

  bool add_file(uint32_t table_id, std::string& path, bool overwrite,
                uint32_t prev = UINT32_MAX) override;

  bool add_file(uint32_t table_id, std::shared_ptr<arrow::Table> arrowTable,
                const std::string& key, uint32_t prev = UINT32_MAX) override;

  bool add_file(uint32_t table_id, std::string& content, const std::string& key,
                uint32_t prev = UINT32_MAX);

  // a string literal as key would otherwise select
  // add_file(table_id, path, bool overwrite) (const char* -> bool)
  bool add_file(uint32_t table_id, std::string& content, const char* key,
                uint32_t prev = UINT32_MAX) {
    return this->add_file(table_id, content, std::string(key), prev);
  }

  // unused: interactive remove
  // bool remove_file() override;

  bool remove_file(uint32_t table_id, std::string& path,
                   uint32_t prev = UINT32_MAX) override;

  bool remove_file(uint32_t table_id, const std::string& key,
                   std::string* path_ptr = nullptr,
                   uint32_t prev = UINT32_MAX) override;

  bool commit(bool read_only = false, bool single_threaded = false) override;

  // unused
  // bool commit(std::vector<double>& timestamps) override;

  bool commit(double& data_read, double& data_write,
              bool single_threaded = false);

  bool reroll() override;

  bool redo(int atStep) override;

  // unused
  // bool undo(int untilStep) override;

  bool abort(bool clear_subdir = true) override;

  uint32_t register_update_pairs(
      std::vector<std::pair<std::string, std::string>>& values) override;

  uint32_t register_update_pairs(
      std::vector<std::pair<std::string, std::string>>& values,
      LHTransactions::UpdateOperations op,
      std::vector<std::pair<std::string, std::string>>& impl) override;

  bool open_new_table(const std::string& path) override;

  // unused
  // void print_stats() override;

  uint32_t get_table_id(std::string& key) override;

  // writes a Delta checkpoint of the table at the version read by this
  // transaction (and updates _last_checkpoint); false if the snapshot is not
  // final or uses Delta features LakeVilla does not track. Commits write
  // checkpoints automatically (CHECKPOINTS, CHECKPOINT_INTERVAL).
  bool create_checkpoint(uint32_t tbl_id);

  // number of attempts for reading/listing the Delta log (exponential backoff)
  // lease: a lock that does not change for this long is considered stale
  // (crashed holder); timeout: maximum time to wait for the lock
  void set_global_lock_timing(uint32_t lease_ms, uint32_t timeout_ms) {
    this->global_lock_lease_ms = lease_ms;
    this->global_lock_timeout_ms = timeout_ms;
  }

  void set_storage_retries(uint32_t retries) {
    this->storage_retries = std::max<uint32_t>(1, retries);
  }

  // ---- tables -------------------------------------------------------------

  struct TableOptions {
    // COMMENT of the table (metaData.description)
    std::string description;
    // TBLPROPERTIES (metaData.configuration), in this order; "delta.*"
    // properties change how Delta clients treat the table and are not
    // supported, nor are the keys Spark reserves (comment, location, ...)
    std::vector<std::pair<std::string, std::string>> properties;
  };

  enum class TableResult {
    Ok,
    // a Delta table exists at the path
    AlreadyExists,
    // the path contains objects that are not a Delta table
    NotEmpty,
    NotFound,
    // invalid path, schema or properties
    Invalid,
    Error
  };

  // Creates an empty Delta table at path (a prefix in the bucket, e.g.
  // "wh/db.db/table/"), like Spark's
  //   CREATE TABLE delta.`s3a://<bucket>/<path>` (<schema>) USING DELTA
  //     [COMMENT '<description>'] [TBLPROPERTIES (<properties>)]
  // (version 0 with commitInfo, metaData and protocol, written with a
  // put-if-absent; see LHHelpers::to_delta_schema for the type mapping).
  // A global-log entry left for the path (a table removed without
  // drop_table) is removed first. Partitioned tables are not supported.
  TableResult create_table(const std::string& path, const arrow::Schema& schema,
                           const TableOptions& options = {},
                           std::string* error = nullptr);

  // Removes the Delta table at path like Spark's DROP TABLE of a managed
  // table: its global-log entry, then all objects below the path (first the
  // checkpoints and version 0, so an interrupted drop leaves no readable
  // table; running it again completes it). Not atomic with respect to
  // concurrent transactions on the table: stop them first.
  TableResult drop_table(const std::string& path, std::string* error = nullptr);

 private:
  // unused (never read)
  // bool cleared = false;
  // int times_redo_num = 0;
  // double data_read;
  // double data_write;
  std::unique_ptr<LHTransactions::GlobalRelation> relation;

  // unused
  // std::stack<std::pair<std::string, std::string>> old_found;

  std::vector<std::vector<std::pair<std::string, std::string>>> update_vals;

  std::vector<LHTransactions::UpdateOperations> update_ops;

  std::vector<std::string> prev_markers;

  // unused (cleared, never read)
  // std::vector<double> commit_diff;

  std::pair<std::string, std::string> found;

  std::unordered_map<std::string, uint32_t> local_log;

  // unused (written, never read)
  // bool global_log_exists;

  // std::string big_cache;

  bool begin(std::string base_path);

  uint32_t storage_retries = LV_STORAGE_RETRIES;

  // protects the transaction's local state (created/deleted files, sublog
  // steps, update values) against concurrent operations of one transaction
  std::mutex state_mutex;

  // protects prev_markers and relation while parsing logs in parallel
  std::mutex parse_mutex;

  std::unique_ptr<StorageConnector::Container> read_retry(
      const std::string& path);

  std::optional<std::vector<StorageConnector::ObjectInfo>> list_retry(
      const std::string& prefix);

  // _last_checkpoint of the table (a hint: it may be missing or behind);
  // etag receives the ETag of the file ("" if it does not exist)
  CheckpointInfo read_last_checkpoint(const std::string& base_path,
                                      std::string* etag = nullptr);

  // the checkpoint a snapshot of version max_version starts from:
  // _last_checkpoint, or the newest complete checkpoint of the log if
  // _last_checkpoint is newer than max_version
  CheckpointInfo find_checkpoint(const std::string& base_path,
                                 uint64_t max_version);

  bool parse_checkpoint(const std::string& base_path,
                        LHExecutor::LHDL::DeltaLogManager& delta_log,
                        const CheckpointInfo& checkpoint);

  // the snapshot of a table at 'version' from its newest checkpoint and the
  // log; nullptr if a version up to it is not final (a marker) or
  // unreadable
  std::shared_ptr<LHExecutor::LHDL::DeltaLogManager> load_snapshot(
      const std::string& base_path, uint32_t version);

  // writes the checkpoint of the snapshot of 'version' (put-if-absent) and
  // moves _last_checkpoint forward
  bool write_checkpoint(const std::string& base_path, uint32_t version,
                        LHExecutor::LHDL::DeltaLogManager& delta_log);

  // after a commit of 'version': writes a checkpoint if the version is a
  // multiple of the table's checkpoint interval (failures do not matter)
  void checkpoint_after_commit(TableStats& table, uint32_t version);

  bool parse_logentries2(uint32_t start, uint32_t end,
                         LHTransactions::TableStats* stats,
                         uint32_t max_version, bool& dep);

  // (re)loads the snapshot of a table and reserves its next version;
  // returns false if the table does not exist or cannot be read
  bool load_table(LHTransactions::TableStats& table, uint32_t num_threads,
                  bool& dependencies);

  std::unique_ptr<TableStats> open_tableV2(std::string&& table_path,
                                           uint32_t num_threads = 2);

  bool reopen_tables(uint32_t num_threads = 2);

  bool wait_for_marker(std::string& path);

  // unused: only used by the interactive functions
  // std::unique_ptr<TableStats> open_table(std::string&& table_path);

  // bool open_new_table();

  // TableStats* select_table();

  // nullptr if table_id is unknown
  TableStats* get_table(uint32_t table_id);

  // like get_table(), and records that the transaction read the table
  TableStats* get_table_for_read(uint32_t table_id);

  // registeres information containied in DL logs; checks for markers
  // returns: true if the json contained a DL entry; false if the entry was an
  // active marker
  uint32_t parseEntry(rapidjson::Document& val,
                      LHExecutor::LHDL::DeltaLogManager* manager,
                      uint32_t version = 0);

  // unused
  // std::pair<std::string, LHExecutor::LHDL::DLOperation> parseEntry(
  //     rapidjson::Document& val);

  void createHeaderMarker(std::string& out_string);

  void createMarkerEntry(std::string& working_dir, std::string& out_str,
                         int64_t deadline_ms);

  // unused
  // bool checkMarker(const rapidjson::Document* content);

  void createHeaderCommit(std::string& out_string, TableStats& stats);

  void createOperationDeleteEntry(FileStats& file, std::string& out_str,
                                  const std::string& base_path);

  void createOperationAddEntry(FileStats& file, std::string& out_str,
                               const std::string& base_path);

  // commitInfo + add/remove actions of a table
  std::string build_commit_log(TableStats& table);

  enum class CommitWrite {
    Ok,
    // the marker's lease was taken over by another transaction
    Lost,
    // without markers: the version was already committed (the Delta log is
    // ahead of the global log)
    Taken,
    Failed
  };

  // writes the commit to the reserved version of the table
  CommitWrite write_commit(TableStats& table, const std::string& log);

  // reserves the next free version after highest_version (marker with a
  // lease for levels 0/1); highest_version is the last version before it
  bool reserve_version(TableStats& table, uint32_t& highest_version);

  // the table's lease ends before a commit could be written
  bool lease_expiring(TableStats& table);

  // the reserved version was taken over by another transaction
  bool marker_lost(TableStats& table);

  // gives up the reserved version and reserves a later one; the versions in
  // between are waited for and checked at commit
  bool shift_marker(TableStats& table);

  // unique path for a new data file of the table
  std::string new_data_file_path(TableStats& table);

  void register_created_file(TableStats& table,
                             std::unique_ptr<FileStats> stats,
                             bool overwrite = false);

  void register_deleted_file(TableStats& table, const std::string& path);

  void log_add_substep(TableStats& table, std::string& path, uint32_t prev);
  void log_delete_substep(TableStats& table, std::string& path, uint32_t prev);

  // global-log lock (level 2): a put-if-absent lock file with a lease
  std::string global_lock_token;
  uint32_t global_lock_lease_ms = 30000;
  uint32_t global_lock_timeout_ms = 120000;

  bool acquire_global_lock();
  void release_global_lock();

  bool add_substep_implicit(std::string& file, SubOpType type,
                            TableStats& table, const std::string* key);

  bool add_substep_explicit(std::string& file, SubOpType type,
                            TableStats& table, const std::string* key);

  bool add_substep(std::string& file, SubOpType type, TableStats& table,
                   const std::string* key);

  void clear_subdirectory();

  bool write_sublog(const std::string& key, const std::string& content);

  // operations can be redone against the newest state at commit time
  bool can_redo();

  bool has_deletes();

  // a concurrent commit removed a file this transaction removes as well
  // (lost update)
  bool has_write_conflict();

  // a table the transaction read was changed concurrently
  bool has_read_conflict();

  // concurrent changes were incorporated (redo)
  void clear_concurrency();

  // without markers and global log: parses versions committed after the
  // read version of tables that are only read
  void collect_unseen_versions();

  // reloads all tables up to their reserved versions and discards the local
  // changes (before another redo round)
  bool rebuild_state();

  enum class LogVersion {
    Missing,
    Marker,
    // a commit without add/remove actions (release, metadata)
    Empty,
    LakeVillaCommit,
    ForeignCommit
  };

  LogVersion classify_version(const std::string& path);

  // with the global-log lock held: replaces the table's global-log entry by
  // update(current entry) (std::nullopt: no entry / remove it)
  bool update_global_entry(
      const std::string& table_path,
      const std::function<std::optional<uint32_t>(std::optional<uint32_t>)>&
          update);

  // LV[Read] (level 2 without markers) may repair the global log
  bool can_repair_global_log();

  // under the global-log lock: advances the table's global-log entry to the
  // highest complete version behind it (not past a running marker), applies
  // these versions to the snapshot and moves the claim after them
  bool repair_global_log(TableStats& table);

  // GLOBAL_LOG_CHECK_AT_BEGIN: versions after the global log's version that
  // were committed by other engines (or are empty) join the snapshot
  uint32_t skip_foreign_versions(const std::string& base_path,
                                 uint32_t version);

  bool execute_reroll_step(std::string exec_string);

  bool parse_global_snapshots();

  enum class Validation { Valid, Invalid, LockTimeout };

  Validation start_validation(
      std::unordered_map<std::string, uint32_t>& version_read_map,
      std::unordered_map<std::string, uint32_t>& version_write_map,
      std::string& content);

  bool create_global_snapshot(
      std::unordered_map<std::string, uint32_t>& version_read_map,
      std::unordered_map<std::string, uint32_t>& version_write_map,
      std::string& content);

  // reads files in list between [start, end[, conformed to the schema
  bool read_part(std::shared_ptr<arrow::Table>& table_ptr,
                 std::vector<std::string>& list, uint32_t start, uint32_t end,
                 TableStats& table);

  // protects the schema caches of the tables
  std::mutex schema_mutex;

  // logical schema of a table's snapshot (parsed once per metaData action);
  // nullptr if the table has no metaData (data files are used as stored) or
  // if it cannot be parsed (*error)
  std::shared_ptr<const LHHelpers::TableSchema> table_schema(
      TableStats& table, std::string* error = nullptr);

  // reads a data file of the table and conforms it to the table schema
  // (LHHelpers::conform_to_schema); nullptr on errors
  std::shared_ptr<arrow::Table> read_data_file(TableStats& table,
                                               const std::string& path);

  // ---- key-based operations: the key is the table's first column --------

  // a data file with rows of a key
  struct KeyFile {
    std::string path;
    // the file conformed to the table schema (nullptr: not read yet)
    std::shared_ptr<arrow::Table> data;
    // the rows of the key in data
    std::vector<int64_t> rows;
    // the statistics prove that the file holds exactly one row, the key's
    bool exact = false;

    // all rows of the file have the key
    bool all_rows() const {
      return this->exact ||
             (this->data &&
              static_cast<int64_t>(this->rows.size()) ==
                  this->data->num_rows());
    }
  };

  // all files of the snapshot (and of the transaction) with rows of the key
  // in the first column of the table: candidates by their min/max
  // statistics (files without statistics are candidates as well) are read
  // to find the rows; files whose statistics prove a single row with the key
  // are not read unless read_exact. Files the transaction created for the
  // update skip_update are ignored. std::nullopt on read errors.
  std::optional<std::vector<KeyFile>> find_key_files(
      TableStats& table, const std::string& key, bool read_exact,
      uint32_t skip_update = UINT32_MAX);

  // reads the files of find_key_files that were not read
  bool read_key_files(TableStats& table, std::vector<KeyFile>& files);

  // the rows of the key in the files; nullptr if there are none
  std::shared_ptr<arrow::Table> key_rows(const std::vector<KeyFile>& files);

  // removes the rows of the key in a file: the file is removed, its other
  // rows are written to a new file (log: sublog steps of explicit logging)
  bool remove_key_rows(TableStats& table, KeyFile& file, uint32_t prev,
                       bool log);

  // redo: the files of the key of the last search step and the files whose
  // key rows were removed since
  std::vector<KeyFile> found_files;
  std::unordered_set<std::string> retired_files;

  // rows to write as a data file, conformed to the table schema; nullptr if
  // they do not fit or the table has partition columns or column mapping
  // (LakeVilla does not write their files)
  std::shared_ptr<arrow::Table> conform_for_write(
      TableStats& table, const std::shared_ptr<arrow::Table>& rows);

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