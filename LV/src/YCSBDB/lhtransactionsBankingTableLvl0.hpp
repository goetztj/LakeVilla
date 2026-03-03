#pragma once
#ifndef YCSB_C_LHTRANSACTIONSBANKINGTABLELVL0_DB_H_
#define YCSB_C_LHTRANSACTIONSBANKINGTABLELVL0_DB_H_

#include <arrow/api.h>
#include <arrow/array/builder_binary.h>
#include <arrow/io/buffered.h>
#include <arrow/io/memory.h>
#include <arrow/util/type_fwd.h>
#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/s3/S3Client.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/schema.h>
#include <parquet/arrow/writer.h>
#include <parquet/stream_reader.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>

#include "Connectors/MinIOConnector.hpp"
#include "LakeVilla/TransactionManager/ManagerGeneric.hpp"
#include "Settings/LvSettings.hpp"
#include "YCSB-C/core/db.h"
#include "YCSB-C/core/properties.h"
#include "settings.hpp"

using std::cout;
using std::endl;

namespace ycsbc {

class LHTransactionsBankingTableDBLvl0 : public DB {
 public:
  LHTransactionsBankingTableDBLvl0(std::string& config_path);

  void Init();

  void Close();

  int Read(const std::string& table, const std::string& key,
           const std::vector<std::string>* fields, std::vector<KVPair>& result);

  int Scan(const std::string& table, const std::string& key, int len,
           const std::vector<std::string>* fields,
           std::vector<std::vector<KVPair>>& result);

  int Update(const std::string& table, const std::string& key,
             std::vector<KVPair>& values);

  int Insert(const std::string& table, const std::string& key,
             std::vector<KVPair>& values);

  int Delete(const std::string& table, const std::string& key);

  void print_stats();

  static std::atomic<uint32_t> next_id;

 private:
  std::unique_ptr<LHConfig::LvSettings> settings;
  int next_actor = 0;

  double changes0 = 0;
  double changes1 = 0;

  std::vector<double> read_perf, update_perf, insert_perf, init_perf,
      commit_perf, scan_perf, commit_diff;

  std::vector<std::vector<double>> commit_perf2;

  std::unique_ptr<LHTransactions::TransactionManager> manager0;
  std::unique_ptr<LHTransactions::TransactionManager> manager1;

  std::shared_ptr<arrow::Schema> table_schema;

  std::shared_ptr<arrow::Table> arrow_table_builder(const std::string& key,
                                                    std::vector<KVPair>& values,
                                                    bool addKey = true);
};

}  // namespace ycsbc

#endif  // YCSB_C_LHTRANSACTIONS_DB_H_
