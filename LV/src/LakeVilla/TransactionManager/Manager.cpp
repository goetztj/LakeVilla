#include "Manager.hpp"

#include <atomic>
#include <mutex>

namespace {
std::mutex default_paths_mutex;
std::string default_global_log = LV_DEFAULT_GLOBAL_LOG;
std::string default_global_tmp_log = LV_DEFAULT_GLOBAL_TMP_LOG;
}  // namespace

void LHTransactions::set_default_global_log_paths(
    const std::string& log_path, const std::string& tmp_log_path) {
  std::lock_guard<std::mutex> guard(default_paths_mutex);
  default_global_log = log_path;
  default_global_tmp_log = tmp_log_path;
}

namespace {
std::mutex default_options_mutex;
LHTransactions::ProtocolOptions default_options;
}  // namespace

void LHTransactions::set_default_protocol_options(
    const ProtocolOptions& options) {
  std::lock_guard<std::mutex> guard(default_options_mutex);
  default_options = options;
}

LHTransactions::ProtocolOptions LHTransactions::default_protocol_options() {
  std::lock_guard<std::mutex> guard(default_options_mutex);
  return default_options;
}

std::pair<std::string, std::string>
LHTransactions::default_global_log_paths() {
  std::lock_guard<std::mutex> guard(default_paths_mutex);
  return {default_global_log, default_global_tmp_log};
}

uint32_t LHTransactions::unique_transaction_id() {
  static std::atomic<uint32_t> next{std::random_device{}()};
  uint32_t id = next.fetch_add(1);
  // UINT32_MAX means "no transaction" in the marker handling
  return id == UINT32_MAX ? next.fetch_add(1) : id;
}

std::vector<std::string> LHTransactions::FileStats::min_values() const {
  std::vector<std::string> result;
  for (auto& column : this->stats.columns) {
    if (column.has_min_max) {
      result.push_back(column.min);
    }
  }
  return result;
}

std::vector<std::string> LHTransactions::FileStats::max_values() const {
  std::vector<std::string> result;
  for (auto& column : this->stats.columns) {
    if (column.has_min_max) {
      result.push_back(column.max);
    }
  }
  return result;
}

LHTransactions::TableStats::TableStats(std::string&& base_path) {
  this->base_path = base_path;
  this->delta_log =
      std::make_unique<LHExecutor::LHDL::DeltaLogManager>(std::move(base_path));
  this->file_num = 0;
  this->in_bytes = 0;
  this->out_bytes = 0;
  this->marker = nullptr;
  this->prev_marker = false;
  opened = true;
  this->marker_version = 0;
};

LHTransactions::TableStats::~TableStats() {
  this->delta_log = nullptr;
  this->marker = nullptr;
};

LHTransactions::TransactionManager::TransactionManager(
    std::string& path_to_table, StorageConnector::Connector* ptr,
    uint32_t transaction_id)
    : base_path(path_to_table), transaction_id(transaction_id) {
  this->connector = nullptr;
  this->working_dir = "";
  this->tables.clear();
  this->next_step = 0;
  this->explicit_log = true;
  this->times_redo = 0;
  this->connector_ptr = ptr;
};

LHTransactions::GlobalRelation::GlobalRelation() {
  this->dependency.clear();
  this->precedence.clear();
}

LHTransactions::GlobalRelation::~GlobalRelation() {
  this->dependency.clear();
  this->precedence.clear();
}

bool LHTransactions::GlobalRelation::register_precedence(uint32_t txn_id) {
  if (this->dependency.count(txn_id) != 0) {
    return false;
  }

  if (this->precedence.count(txn_id) != 0) {
    this->precedence.find(txn_id)->second++;
  } else {
    this->precedence.insert({txn_id, 1});
  }

  return true;
}

/// @brief register a dependency between two concurrent transactions
/// @param txn_id
/// @param marker_path
/// @return does this dependency span across multiple tables?
bool LHTransactions::GlobalRelation::register_dependency(uint32_t txn_id) {
  if (this->dependency.count(txn_id) != 0) {
    this->dependency.find(txn_id)->second++;
    return true;
  } else {
    this->dependency.insert({txn_id, 1});

    return false;
  }
}

std::vector<uint32_t> LHTransactions::GlobalRelation::get_dependencies() {
  std::vector<uint32_t> res;

  for (auto& elem : this->dependency) {
    res.push_back(elem.first);
  }

  return res;
}