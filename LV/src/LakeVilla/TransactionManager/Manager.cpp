#include "Manager.hpp"

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
    std::string& path_to_table, StorageConnector::MinIOConfig config,
    uint32_t transaction_id)
    : base_path(path_to_table), transaction_id(transaction_id) {
  this->connector = std::make_unique<StorageConnector::MinIOConnector>(&config);
  this->working_dir = "";
  this->tables.clear();
  this->next_step = 0;
  this->explicit_log = true;

  this->times_redo = 0;
  this->connector_ptr = this->connector.get();
};

LHTransactions::TransactionManager::TransactionManager(
    std::string& path_to_table, StorageConnector::MinIOConnector* ptr,
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