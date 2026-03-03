#pragma once
#include "Phase.hpp"

namespace LHLST {

// SingleUser phase of LSTBench
struct SingleUser : Phase {
  std::unique_ptr<LHTransactions::TransactionManagerGeneric> txnmanager;
  uint32_t threads;
  
  SingleUser(TpcdsPaths paths, StorageConnector::MinIOConnector* connector,
       uint32_t num_queries, uint32_t id, uint32_t threads, std::vector<bool> level_config);

  void run(std::vector<double>& times) override;

  private:
  bool q9();
  bool q67();
  bool q68();
  bool q90();
};
};  // namespace LHLST