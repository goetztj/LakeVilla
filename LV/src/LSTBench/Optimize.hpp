#pragma once
#include "Phase.hpp"

namespace LHLST {

// Optimize phase of LSTBench
struct Optimize : Phase {
  std::unique_ptr<LHTransactions::TransactionManagerGeneric> txnmanager;

  preparedLSTContent* content;

  Optimize(TpcdsPaths paths, StorageConnector::MinIOConnector* connector,
           uint32_t num_queries, uint32_t id, preparedLSTContent* content,
           std::vector<bool> level_config);

  void run(std::vector<double>& times) override;
};
};  // namespace LHLST