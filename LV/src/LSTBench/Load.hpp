#pragma once
#include "Phase.hpp"

namespace LHLST {

// Load phase of LSTBench
struct Load : Phase {
  std::unique_ptr<LHTransactions::TransactionManagerGeneric> txnmanager;

  preparedLSTContent* content;

  Load(TpcdsPaths paths, StorageConnector::MinIOConnector* connector,
       uint32_t num_queries, uint32_t id, preparedLSTContent* content,
       std::vector<bool> level_config);

  void run(std::vector<double>& times) override;

 private:
  bool iRow(std::string key, std::string& table, std::string& content);
};
};  // namespace LHLST