#pragma once
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <streambuf>
#include <thread>
#include <utility>

#include "Connectors/MinIOConnector.hpp"
#include "Helpers/Helper.hpp"
#include "LakeVilla/TransactionManager/ManagerGeneric.hpp"

#define CAB_READ_PATH "./../src/CAB/cab/benchmark-query-streams/small_1tb_10cpu/streamssf1/"

namespace LHCAB {

struct preparedContent {
  std::string customer;
  std::string lineitem;
  std::string nation;
  std::string orders;
  std::string part;
  std::string partsupp;
  std::string region;
  std::string supplier;

  preparedContent(std::string& base_path);
};

// This client simulates the TPC-C accesses with the LakeVilla client
struct CABClient {
  std::string customer_path;
  std::string lineitem_path;
  std::string nation_path;
  std::string orders_path;
  std::string part_path;
  std::string partsupp_path;
  std::string region_path;
  std::string supplier_path;

  std::string base_path;
  uint64_t tendant;

  preparedContent* content;

  uint32_t id;

  std::string query_file;

  StorageConnector::MinIOConfig config;

  double read_mb_count;
  double write_mb_count;

  std::vector<bool> level_config = {false, false, false};

  std::unique_ptr<StorageConnector::MinIOConnector> connector;

  CABClient(std::string& base_path, uint64_t tendant,
            StorageConnector::MinIOConfig config, uint32_t id,
            preparedContent* content);

  void generateTablePaths(std::string& base_path, uint64_t tendant);

  bool start_query(int id);

  bool start_query(int id, std::vector<bool> levels);

  bool q1();

  bool q2();

  bool q3();

  bool q4();

  bool q5();

  bool q6();

  bool q7();

  bool q8();

  bool q9();

  bool q10();

  bool q11();

  bool q12();

  bool q13();

  bool q14();

  bool q15();

  bool q16();

  bool q17();

  bool q18();

  bool q19();

  bool q20();

  bool q21();

  bool q22();

  bool q23();

  bool iCustomer();

  bool iLineitem();

  bool iNation();

  bool iOrders();

  bool iPart();

  bool iPartsupp();

  bool iRegion();

  bool iSupplier();

  bool iRow(LHTransactions::TransactionManagerGeneric* manager,
            std::string& table, std::string& content);

  bool uTable(std::string& tbl_path);

  bool cleanup();
};
};  // namespace LHCAB