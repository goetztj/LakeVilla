//
//  Synthetic banking benchmark for LakeVilla (YCSB-C binding).
//
//  Two accounts (key "0" and "1" of one table, or key "0" of two tables)
//  are used by the YCSB operations:
//    Read:   check the balance of an account
//    Scan:   withdraw money from an account
//    Update: transfer money between the accounts
//
#ifndef YCSB_C_LHTRANSACTIONS_BANKING_DB_H_
#define YCSB_C_LHTRANSACTIONS_BANKING_DB_H_

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "LakeVilla/TransactionManager/ManagerDeltaLake.hpp"
#include "Settings/LvSettings.hpp"
#include "YCSB-C/core/db.h"
#include "YCSBDB/stats_db.hpp"

namespace ycsbc {

class LHTransactionsBankingDB : public DB, public StatsDB {
 public:
  enum class Layout {
    // both accounts in one table (multi-query transactions)
    Query,
    // one account per table, each transfer is one multi-table transaction
    MultiTable,
    // one account per table, a transfer is one transaction per table
    // (no multi-table transactions)
    SeparateTables
  };

  // init_global_log: seed the global version log with both tables (level 2)
  LHTransactionsBankingDB(std::string& config_path, std::vector<bool> levels,
                          Layout layout, bool init_global_log = false);

  void Init() override;

  void Close() override;

  int Read(const std::string& table, const std::string& key,
           const std::vector<std::string>* fields,
           std::vector<KVPair>& result) override;

  int Scan(const std::string& table, const std::string& key, int len,
           const std::vector<std::string>* fields,
           std::vector<std::vector<KVPair>>& result) override;

  int Update(const std::string& table, const std::string& key,
             std::vector<KVPair>& values) override;

  int Insert(const std::string& table, const std::string& key,
             std::vector<KVPair>& values) override;

  int Delete(const std::string& table, const std::string& key) override;

  void print_stats() override;

 private:
  struct Account {
    LHTransactions::TransactionManager* manager;
    uint32_t table_id;
    std::string key;
    double* changes;
  };

  std::unique_ptr<LHConfig::LvSettings> settings;
  std::vector<bool> levels;
  Layout layout;
  bool init_global_log;

  int next_actor = 0;

  // committed balance changes of both accounts
  double changes0 = 0;
  double changes1 = 0;

  std::vector<double> read_perf, update_perf, insert_perf, init_perf,
      commit_perf, scan_perf, commit_diff;

  std::unique_ptr<LHTransactions::TransactionManager> manager0;
  std::unique_ptr<LHTransactions::TransactionManager> manager1;

  std::shared_ptr<arrow::Schema> table_schema;

  // the acting account of this operation (alternating) and the other one
  Account actor(int id);

  bool begin(LHTransactions::TransactionManager* manager);
  bool commit(LHTransactions::TransactionManager* manager);

  std::optional<double> read_balance(Account& account, std::string& path);

  // writes the new balance (op/change describe the update for redo)
  bool write_balance(Account& account, double balance,
                     LHTransactions::UpdateOperations op, double change,
                     std::string& path);

  std::shared_ptr<arrow::Table> arrow_table_builder(const std::string& key,
                                                    const std::string& balance);
};

}  // namespace ycsbc

#endif  // YCSB_C_LHTRANSACTIONS_BANKING_DB_H_
