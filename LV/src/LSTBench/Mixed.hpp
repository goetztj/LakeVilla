#pragma once
#include "Phase.hpp"

namespace LHLST {

// Mixed phase of LSTBench
struct Mixed : Phase {
  std::unique_ptr<LHTransactions::TransactionManagerGeneric> txnmanager;

  preparedLSTContent* content;

  Mixed(TpcdsPaths paths, StorageConnector::MinIOConnector* connector,
        uint32_t num_queries, uint32_t id, preparedLSTContent* content,
        std::vector<bool> level_config);

  void run(std::vector<double>& times) override;

 private:
  void run_single_table(std::vector<double>& times);

  bool iRow(std::string key, std::string& table, std::string& content);

  bool uTable(std::string& tbl_path);

  bool rTable(std::string& tbl_path);
};
};  // namespace LHLST