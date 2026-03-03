#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>

#include <chrono>
#include <iostream>
#include <string>
#include <utility>

#include "Connectors/MinIOConnector.hpp"
#include "Helpers/Helper.hpp"
#include "LakeVilla/TransactionManager/ManagerGeneric.hpp"
#include "Settings/LvSettings.hpp"
#include "Timer/Timer.hpp"
#include "settings.hpp"

#define TEST_LOCK "wh/tmp.db/lock/"
#define TEST_LOCK2 "wh/tmp.db/lock2/"
#define TEST_SNAP1 "wh/tmp.db/snap1/"
#define TEST_SNAP2 "wh/tmp.db/snap2/"
#define TEST_SNAP3 "wh/tmp.db/snap3/"

using namespace Aws;
using namespace Aws::Auth;

std::vector<uint32_t> check_consistency(StorageConnector::MinIOConfig& config,
                                        std::vector<bool>& levels,
                                        std::vector<uint32_t> expected,
                                        bool no_check = false) {
  std::string path_lock = TEST_LOCK;
  std::string path_snap1 = TEST_SNAP1;
  std::string path_snap2 = TEST_SNAP2;
  std::string path_lock2 = TEST_LOCK2;
  std::string path_snap3 = TEST_SNAP3;

  std::vector<bool> levels_adapted = {false, false, levels[2]};

  // T1
  LHTransactions::TransactionManagerGeneric t1(levels_adapted, path_lock,
                                               config, 42);
  t1.begin_transaction_ycsb();

  t1.open_new_table(path_snap1);
  t1.open_new_table(path_snap2);
  t1.open_new_table(path_lock2);
  t1.open_new_table(path_snap3);

  std::vector<uint32_t> read_versions;
  read_versions.reserve(3);
  uint32_t counter = 0;

  for (auto& ref : t1.tables) {
    read_versions.push_back(ref->read_version);
    if (!no_check && ref->read_version != expected[counter]) {
      std::cerr << ">>> consistency check failed for table " << ref->base_path
                << ": " << ref->read_version << "(exp.: " << expected[counter]
                << ")" << std::endl;
    }
    counter++;
  }

  t1.abort();
  return read_versions;
}

std::shared_ptr<arrow::Table> createTable(
    std::vector<std::vector<int>> columns) {
  arrow::FieldVector attributes;

  attributes.push_back(arrow::field("id", arrow::int32()));
  attributes.push_back(arrow::field("value", arrow::int32()));

  auto table_schema = arrow::schema(attributes);

  arrow::ArrayVector value_vec;

  for (auto& col : columns) {
    std::shared_ptr<arrow::Array> arrow_array;

    auto values_builder = std::make_shared<arrow::Int32Builder>();

    for (auto& ref : col) {
      PARQUET_THROW_NOT_OK(values_builder->Append(ref));
    }
    PARQUET_THROW_NOT_OK(values_builder->Finish(&arrow_array));
    value_vec.push_back(arrow_array);
  }

  return arrow::Table::Make(table_schema, value_vec, columns[0].size());
}

bool check_table(StorageConnector::MinIOConfig& config, std::string& table_path,
                 std::vector<std::vector<int>> expected_columns) {
  // read_transaction: read the requested table using the original Delta Lake
  LHTransactions::TransactionManagerGeneric read_txn({false, false, false},
                                                     table_path, config, 99);
  read_txn.begin_transaction_ycsb();

  auto table_id = read_txn.get_table_id(table_path);

  auto result = read_txn.read_table(table_id);
  read_txn.commit();

  // build an arrow table from the given expected rows
  auto schema = result->schema();

  arrow::ArrayVector value_vec;

  for (auto& col : expected_columns) {
    std::shared_ptr<arrow::Array> arrow_array;

    auto values_builder = std::make_shared<arrow::Int32Builder>();

    for (auto& ref : col) {
      PARQUET_THROW_NOT_OK(values_builder->Append(ref));
    }
    PARQUET_THROW_NOT_OK(values_builder->Finish(&arrow_array));
    value_vec.push_back(arrow_array);
  }

  auto expected_table =
      arrow::Table::Make(schema, value_vec, expected_columns.size());

  // compare the result and the expected table
  if (!expected_table->Equals(*result)) {
    std::cerr << "tables did not match!" << std::endl
              << "--- Got: " << std::endl
              << result->ToString() << std::endl
              << "--- Expected:" << std::endl
              << expected_table->ToString() << std::endl;
    return false;
  }
  return true;
}

bool check_table(std::shared_ptr<arrow::Table> tbl,
                 std::vector<std::vector<int>> expected_columns) {
  // build an arrow table from the given expected rows
  auto schema = tbl->schema();

  arrow::ArrayVector value_vec;

  for (auto& col : expected_columns) {
    std::shared_ptr<arrow::Array> arrow_array;

    auto values_builder = std::make_shared<arrow::Int32Builder>();

    for (auto& ref : col) {
      PARQUET_THROW_NOT_OK(values_builder->Append(ref));
    }
    PARQUET_THROW_NOT_OK(values_builder->Finish(&arrow_array));
    value_vec.push_back(arrow_array);
  }

  auto expected_table =
      arrow::Table::Make(schema, value_vec, expected_columns.size());

  // compare the tbl and the expected table
  if (!expected_table->Equals(*tbl)) {
    std::cerr << "tables did not match!" << std::endl
              << "--- Got: " << std::endl
              << tbl->ToString() << std::endl
              << "--- Expected:" << std::endl
              << expected_table->ToString() << std::endl;
    return false;
  }
  return true;
}

bool update(LHTransactions::TransactionManagerGeneric& manager,
            std::string& table, uint32_t key, uint32_t val) {
  std::string path = "";

  std::string key_str = std::to_string(key);
  std::string val_str = std::to_string(val);
  // get table id
  auto tbl_id = manager.get_table_id(table);

  // read orginal file
  auto arrowBaseTable = manager.read_file_as_table(tbl_id, key_str, path);

  if (!arrowBaseTable) {
    return false;
  }

  // register update operation

  std::vector<std::pair<std::string, std::string>> update_vals;
  update_vals.push_back({"value", val_str});
  auto prev_id = manager.register_update_pairs(
      update_vals, LHTransactions::UpdateOperations::REP, update_vals);

  std::thread sub_remove_thread([&manager, &tbl_id, &key, &path, &prev_id] {
    manager.remove_file(tbl_id, path, prev_id);
  });

  auto col_names = arrowBaseTable->ColumnNames();
  for (int i = 0; i < col_names.size(); i++) {
    if (col_names[i].compare("value") == 0) {
      auto field = std::make_shared<arrow::Field>("value", arrow::int32());

      arrow::Int32Builder builder;
      PARQUET_THROW_NOT_OK(builder.Append(val));
      std::shared_ptr<arrow::Array> arrow_array;
      std::vector<std::shared_ptr<arrow::Array>> chunks;

      PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));
      chunks.push_back(std::move(arrow_array));

      auto new_table = arrowBaseTable->SetColumn(
          i, field, std::make_shared<arrow::ChunkedArray>(std::move(chunks)));

      arrowBaseTable = *new_table;
      break;
    }
  }

  bool add_success = manager.add_file(tbl_id, arrowBaseTable, key_str, prev_id);

  sub_remove_thread.join();

  return add_success;
}

bool hermitage_g0(std::vector<bool> levels, LHConfig::LvSettings& settings) {
  bool success = true;

  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "------ Q0 ------" << std::endl;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    std::string path_lock = TEST_LOCK;
    std::string path_snap1 = TEST_SNAP1;
    std::string path_snap2 = TEST_SNAP2;

    auto versions = check_consistency(config2, levels, {}, true);

    // T1
    LHTransactions::TransactionManagerGeneric t1(levels, path_lock, config2, 1);
    t1.begin_transaction_ycsb();
    // T2
    LHTransactions::TransactionManagerGeneric t2(levels, path_lock, config2, 2);
    t2.begin_transaction_ycsb();

    versions[0] += 2;
    versions = check_consistency(config2, levels, versions);

    // update test_lock.dbo.test set value = 11 where id = 1; -- T1
    update(t1, path_lock, 1, 11);

    // update test_lock.dbo.test set value = 12 where id = 1; -- T2, BLOCKS
    update(t2, path_lock, 1, 12);

    // update test_lock.dbo.test set value = 21 where id = 2; -- T1
    update(t1, path_lock, 2, 21);

    t1.commit();
    versions = check_consistency(config2, levels, versions);

    // select * from test_lock.dbo.test;
    success =
        check_table(config2, path_lock, {{1, 2}, {11, 21}}) ? success : false;

    // update test_lock.dbo.test set value = 22 where id = 2;
    update(t2, path_lock, 2, 22);

    t2.commit();
    versions = check_consistency(config2, levels, versions);

    // select * from test_lock.dbo.test;
    success =
        check_table(config2, path_lock, {{1, 2}, {12, 22}}) ? success : false;
  }
  Aws::ShutdownAPI(options);

  return success;
}

bool hermitage_g0_mt(std::vector<bool> levels, LHConfig::LvSettings& settings) {
  bool success = true;

  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "------ Q0-MT ------" << std::endl;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    std::string path_lock = TEST_LOCK2;
    std::string path_snap1 = TEST_SNAP1;
    std::string path_snap2 = TEST_SNAP2;
    std::string path_snap3 = TEST_SNAP3;

    auto versions = check_consistency(config2, levels, {}, true);

    // T1
    LHTransactions::TransactionManagerGeneric t1(levels, path_snap3, config2,
                                                 1);
    t1.begin_transaction_ycsb();
    // T2
    LHTransactions::TransactionManagerGeneric t2(levels, path_lock, config2, 2);
    t2.begin_transaction_ycsb();

    t1.open_new_table(path_lock);

    versions[4] += 2;
    versions[5] += 1;
    versions = check_consistency(config2, levels, versions);

    // update test_lock.dbo.test set value = 11 where id = 1; -- T1
    update(t1, path_snap3, 1, 11);

    // update test_lock.dbo.test set value = 12 where id = 1; -- T2, BLOCKS
    update(t2, path_lock, 1, 12);

    // update test_lock.dbo.test set value = 21 where id = 2; -- T1
    update(t1, path_lock, 2, 21);

    t1.commit();
    versions = check_consistency(config2, levels, versions);

    // select * from test_lock.dbo.test;
    success =
        check_table(config2, path_lock, {{1, 2}, {11, 21}}) ? success : false;

    // update test_lock.dbo.test set value = 22 where id = 2;
    update(t2, path_lock, 2, 22);

    t2.commit();
    versions = check_consistency(config2, levels, versions);

    // select * from test_lock.dbo.test;
    success =
        check_table(config2, path_lock, {{1, 2}, {12, 22}}) ? success : false;
  }
  Aws::ShutdownAPI(options);

  return success;
}

bool hermitage_g1a(std::vector<bool> levels, LHConfig::LvSettings& settings) {
  bool success = true;

  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "------ G1a ------" << std::endl;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    std::string path_lock = TEST_LOCK;
    std::string path_snap1 = TEST_SNAP1;
    std::string path_snap2 = TEST_SNAP2;

    auto versions = check_consistency(config2, levels, {}, true);
    versions[0] += 2;

    // T1
    LHTransactions::TransactionManagerGeneric t1(levels, path_lock, config2, 1);
    t1.begin_transaction_ycsb();
    // T2
    LHTransactions::TransactionManagerGeneric t2(levels, path_lock, config2, 2);
    t2.begin_transaction_ycsb();
    versions = check_consistency(config2, levels, versions);

    // update test_lock.dbo.test set value = 101 where id = 1;
    update(t1, path_lock, 1, 101);

    // select t2
    auto tbl_id = t2.get_table_id(path_lock);
    auto tbl_t2_1 = t2.read_table(tbl_id);
    success = check_table(tbl_t2_1, {{1, 2}, {12, 22}}) ? success : false;

    t1.abort();
    versions = check_consistency(config2, levels, versions);

    // select t2
    auto tbl_t2_2 = t2.read_table(tbl_id);
    success = check_table(tbl_t2_2, {{1, 2}, {12, 22}}) ? success : false;

    // commit using the read_only hint
    t2.commit(true);
    versions = check_consistency(config2, levels, versions);
  }
  Aws::ShutdownAPI(options);
  return success;
}

bool hermitage_g1b(std::vector<bool> levels, LHConfig::LvSettings& settings) {
  bool success = true;
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "------ G1b ------" << std::endl;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    std::string path_lock = TEST_LOCK;
    std::string path_snap1 = TEST_SNAP1;
    std::string path_snap2 = TEST_SNAP2;

    auto versions = check_consistency(config2, levels, {}, true);
    versions[0] += 2;

    // T1
    LHTransactions::TransactionManagerGeneric t1(levels, path_lock, config2, 1);
    t1.begin_transaction_ycsb();
    // T2
    LHTransactions::TransactionManagerGeneric t2(levels, path_lock, config2, 2);
    t2.begin_transaction_ycsb();

    // update test_lock.dbo.test set value = 101 where id = 1;
    update(t1, path_lock, 1, 101);

    // select * from test_lock.dbo.test;
    auto tbl_id = t2.get_table_id(path_lock);
    auto table_t2_1 = t2.read_table(tbl_id);
    success = check_table(table_t2_1, {{1, 2}, {12, 22}}) ? success : false;

    // update test_lock.dbo.test set value = 11 where id = 1; -- T1
    update(t1, path_lock, 1, 11);

    t1.commit();
    versions = check_consistency(config2, levels, versions);

    // select * from test_lock.dbo.test;
    auto table_t2_2 = t2.read_table(tbl_id);
    success = check_table(table_t2_1, {{1, 2}, {12, 22}}) ? success : false;

    // commit using the read_only hint
    t2.commit(true);
    versions = check_consistency(config2, levels, versions);
  }
  Aws::ShutdownAPI(options);
  return success;
}

bool hermitage_g1c(std::vector<bool> levels, LHConfig::LvSettings& settings) {
  bool success = true;
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "------ G1c ------" << std::endl;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    std::string path_lock = TEST_LOCK;
    std::string path_snap1 = TEST_SNAP1;
    std::string path_snap2 = TEST_SNAP2;

    auto versions = check_consistency(config2, levels, {}, true);
    versions[0] += 2;

    // T1
    LHTransactions::TransactionManagerGeneric t1(levels, path_lock, config2, 1);
    t1.begin_transaction_ycsb();
    // T2
    LHTransactions::TransactionManagerGeneric t2(levels, path_lock, config2, 2);
    t2.begin_transaction_ycsb();

    // update test_lock.dbo.test set value = 11 where id = 1; -- T1
    update(t1, path_lock, 1, 111);

    // update test_lock.dbo.test set value = 22 where id = 2; -- T2
    update(t2, path_lock, 2, 222);

    std::string tmp_string = "";
    // select * from test_lock.dbo.test where id = 2; -- T1.
    auto tbl_id = t1.get_table_id(path_lock);
    auto table_t1_1 = t1.read_file_as_table(tbl_id, "2", tmp_string);
    success = check_table(table_t1_1, {{2}, {22}}) ? success : false;

    // select * from test_lock.dbo.test where id = 1; -- T2.
    auto tbl_id2 = t2.get_table_id(path_lock);
    auto table_t2_1 = t2.read_file_as_table(tbl_id2, "1", tmp_string);
    success = check_table(table_t2_1, {{1}, {11}}) ? success : false;

    t1.commit();
    versions = check_consistency(config2, levels, versions);
    t2.commit();
    versions = check_consistency(config2, levels, versions);
  }
  Aws::ShutdownAPI(options);
  return success;
}

bool hermitage_g1c_mt(std::vector<bool> levels,
                      LHConfig::LvSettings& settings) {
  bool success = true;
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "------ G1c-MT ------" << std::endl;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    std::string path_lock = TEST_LOCK;
    std::string path_lock2 = TEST_LOCK2;
    std::string path_snap1 = TEST_SNAP1;
    std::string path_snap2 = TEST_SNAP2;
    std::string path_snap3 = TEST_SNAP3;

    auto versions = check_consistency(config2, levels, {}, true);
    versions[0] += 2;

    // T1
    LHTransactions::TransactionManagerGeneric t1(levels, path_lock, config2, 1);
    t1.begin_transaction_ycsb();
    // T2
    LHTransactions::TransactionManagerGeneric t2(levels, path_lock, config2, 2);
    t2.begin_transaction_ycsb();

    // update test_lock.dbo.test set value = 11 where id = 1; -- T1
    update(t1, path_lock, 1, 111);

    // update test_lock.dbo.test set value = 22 where id = 2; -- T2
    update(t2, path_lock, 2, 222);

    std::string tmp_string = "";
    // select * from test_lock.dbo.test where id = 2; -- T1.
    auto tbl_id = t1.get_table_id(path_lock);
    auto table_t1_1 = t1.read_file_as_table(tbl_id, "2", tmp_string);
    success = check_table(table_t1_1, {{2}, {22}}) ? success : false;

    // select * from test_lock.dbo.test where id = 1; -- T2.
    auto tbl_id2 = t2.get_table_id(path_lock);
    auto table_t2_1 = t2.read_file_as_table(tbl_id2, "1", tmp_string);
    success = check_table(table_t2_1, {{1}, {11}}) ? success : false;

    t1.commit();
    versions = check_consistency(config2, levels, versions);
    t2.commit();
    versions = check_consistency(config2, levels, versions);
  }
  Aws::ShutdownAPI(options);
  return success;
}

bool hermitage_otv(std::vector<bool> levels, LHConfig::LvSettings& settings) {
  bool success = true;
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "------ OTV ------" << std::endl;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    std::string path_lock = TEST_LOCK;
    std::string path_snap1 = TEST_SNAP1;
    std::string path_snap2 = TEST_SNAP2;
    auto versions = check_consistency(config2, levels, {}, true);
    versions[0] += 3;

    // T1
    LHTransactions::TransactionManagerGeneric t1(levels, path_lock, config2, 1);
    t1.begin_transaction_ycsb();
    // T2
    LHTransactions::TransactionManagerGeneric t2(levels, path_lock, config2, 2);
    t2.begin_transaction_ycsb();
    // T3
    LHTransactions::TransactionManagerGeneric t3(levels, path_lock, config2, 3);
    t3.begin_transaction_ycsb();
    versions = check_consistency(config2, levels, versions);

    // update spark_catalog.hermitage.lock set value = 11 where id = 1; -- T1
    update(t1, path_lock, 1, 11);

    // update spark_catalog.hermitage.lock set value = 19 where id = 2; -- T1
    update(t1, path_lock, 2, 19);

    // update spark_catalog.hermitage.lock set value = 12 where id = 1; -- T2
    update(t2, path_lock, 1, 12);

    t1.commit();
    versions = check_consistency(config2, levels, versions);

    std::string tmp_string = "";
    // select * from spark_catalog.hermitage.lock; -- T3. Shows 1 => 12, 2 => 19
    auto tbl_id = t3.get_table_id(path_lock);
    auto table_t3_1 = t3.read_table(tbl_id);
    success = check_table(table_t3_1, {{1, 2}, {111, 222}}) ? success : false;

    // update spark_catalog.hermitage.lock set value = 18 where id = 2; -- T2
    update(t2, path_lock, 2, 18);

    t2.commit();
    versions = check_consistency(config2, levels, versions);
    t3.commit(true);
    versions = check_consistency(config2, levels, versions);

    // select * from spark_catalog.hermitage.lock; -- T3. Shows 1 => 12, 2 => 18
    success =
        check_table(config2, path_lock, {{1, 2}, {12, 18}}) ? success : false;
  }
  Aws::ShutdownAPI(options);
  return success;
}

bool hermitage_pmp(std::vector<bool> levels, LHConfig::LvSettings& settings) {
  bool success = true;
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "------ PMP ------" << std::endl;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    std::string path_lock = TEST_LOCK;
    std::string path_snap1 = TEST_SNAP1;
    std::string path_snap2 = TEST_SNAP2;
    auto versions = check_consistency(config2, levels, {}, true);
    versions[0] += 2;

    // changed order due to non-concurrency
    // T2
    LHTransactions::TransactionManagerGeneric t2(levels, path_lock, config2, 2);
    t2.begin_transaction_ycsb();
    // T1
    LHTransactions::TransactionManagerGeneric t1(levels, path_lock, config2, 1);
    t1.begin_transaction_ycsb();

    versions = check_consistency(config2, levels, versions);

    std::string tmp_string = "";
    // select * from test_lock.dbo.test where value = 30; -- T1. Returns nothing
    // Substitute: read the entire table
    auto tbl_id = t1.get_table_id(path_lock);
    auto table_t1_1 = t1.read_table(tbl_id);
    success = check_table(table_t1_1, {{1, 2}, {12, 18}}) ? success : false;

    // insert into test_lock.dbo.test (id, value) values(3, 30); -- T2
    auto tbl_id_t2 = t2.get_table_id(path_lock);

    auto partial_table = createTable({{3}, {30}});
    t2.add_file(tbl_id_t2, partial_table, "3");

    t2.commit();
    versions = check_consistency(config2, levels, versions);

    // select * from test_lock.dbo.test where value % 3 = 0; -- T1.
    auto table_t1_2 = t1.read_table(tbl_id);
    success = check_table(table_t1_2, {{1, 2}, {12, 18}}) ? success : false;

    t1.commit(true);
    versions = check_consistency(config2, levels, versions);
  }
  Aws::ShutdownAPI(options);
  return success;
}

bool hermitage_p4(std::vector<bool> levels, LHConfig::LvSettings& settings) {
  bool success = true;
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "------ P4 ------" << std::endl;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    std::string path_lock = TEST_LOCK;
    std::string path_snap1 = TEST_SNAP1;
    std::string path_snap2 = TEST_SNAP2;
    auto versions = check_consistency(config2, levels, {}, true);
    versions[1] += 2;

    // T1
    LHTransactions::TransactionManagerGeneric t1(levels, path_snap1, config2,
                                                 1);
    t1.begin_transaction_ycsb();
    // T2
    LHTransactions::TransactionManagerGeneric t2(levels, path_snap1, config2,
                                                 2);
    t2.begin_transaction_ycsb();
    versions = check_consistency(config2, levels, versions);

    std::string tmp_string = "";
    // select * from test_lock.dbo.test where id = 1; -- T1
    auto tbl_id = t1.get_table_id(path_snap1);
    auto table_t1_1 = t1.read_table(tbl_id);
    success = check_table(table_t1_1, {{1, 2}, {10, 20}}) ? success : false;

    // select * from test_lock.dbo.test where id = 1; -- T2
    auto tbl_id_t2 = t2.get_table_id(path_snap1);
    auto table_t2_1 = t2.read_table(tbl_id_t2);
    success = check_table(table_t2_1, {{1, 2}, {10, 20}}) ? success : false;

    // update test_lock.dbo.test set value = 11 where id = 1; -- T1
    update(t1, path_snap1, 1, 11);

    // update test_lock.dbo.test set value = 11 where id = 1; -- T2
    update(t2, path_snap1, 1, 11);

    t1.commit();
    versions = check_consistency(config2, levels, versions);
    t2.commit();
    versions = check_consistency(config2, levels, versions);

    success =
        check_table(config2, path_snap1, {{1, 2}, {11, 20}}) ? success : false;

    // t1.commit(true);
  }
  Aws::ShutdownAPI(options);
  return success;
}

bool hermitage_gsingle(std::vector<bool> levels,
                       LHConfig::LvSettings& settings) {
  bool success = true;
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "------ G-single ------" << std::endl;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    std::string path_lock = TEST_LOCK;
    std::string path_snap1 = TEST_SNAP1;
    std::string path_snap2 = TEST_SNAP2;

    auto versions = check_consistency(config2, levels, {}, true);
    versions[1] += 2;

    // changed order because transactions are not really concurrent
    // T2
    LHTransactions::TransactionManagerGeneric t2(levels, path_snap1, config2,
                                                 2);
    t2.begin_transaction_ycsb();
    // T1
    LHTransactions::TransactionManagerGeneric t1(levels, path_snap1, config2,
                                                 1);
    t1.begin_transaction_ycsb();

    versions = check_consistency(config2, levels, versions);

    std::string tmp_string = "";
    // select * from test_lock.dbo.test where id = 1; -- T1. Shows 1 => 10
    auto tbl_id = t1.get_table_id(path_snap1);
    auto table_t1_1 = t1.read_file_as_table(tbl_id, "1", tmp_string);
    success = check_table(table_t1_1, {{1}, {11}}) ? success : false;

    // select * from test_lock.dbo.test where id = 1; -- T2. Shows 1 => 10
    auto tbl_id_t2 = t2.get_table_id(path_snap1);
    auto table_t2_1 = t2.read_file_as_table(tbl_id_t2, "1", tmp_string);
    success = check_table(table_t2_1, {{1}, {11}}) ? success : false;

    // select * from test_lock.dbo.test where id = 2; -- T2
    auto table_t2_2 = t2.read_file_as_table(tbl_id_t2, "2", tmp_string);
    success = check_table(table_t2_2, {{2}, {20}}) ? success : false;

    // update test_lock.dbo.test set value = 12 where id = 1; -- T2
    update(t2, path_snap1, 1, 12);

    // update test_lock.dbo.test set value = 18 where id = 2; -- T2
    update(t2, path_snap1, 2, 18);

    t2.commit();
    versions = check_consistency(config2, levels, versions);

    // select * from test_lock.dbo.test where id = 2; -- T1. Shows 2 => 18
    auto table_t1_2 = t1.read_file_as_table(tbl_id, "2", tmp_string);
    success = check_table(table_t1_2, {{2}, {20}}) ? success : false;

    t1.commit(true);
    versions = check_consistency(config2, levels, versions);
  }
  Aws::ShutdownAPI(options);
  return success;
}

bool hermitage_gitem(std::vector<bool> levels, LHConfig::LvSettings& settings) {
  bool success = true;
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "------ G-item ------" << std::endl;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    std::string path_lock = TEST_LOCK;
    std::string path_snap1 = TEST_SNAP1;
    std::string path_snap2 = TEST_SNAP2;

    auto versions = check_consistency(config2, levels, {}, true);
    versions[2] += 2;

    // T1
    LHTransactions::TransactionManagerGeneric t1(levels, path_snap2, config2,
                                                 1);
    t1.begin_transaction_ycsb();
    // T2
    LHTransactions::TransactionManagerGeneric t2(levels, path_snap2, config2,
                                                 2);
    t2.begin_transaction_ycsb();

    std::string tmp_string = "";
    // select * from test_lock.dbo.test where id in (1,2); -- T1
    auto tbl_id = t1.get_table_id(path_snap2);
    auto table_t1_1 = t1.read_table(tbl_id);
    success = check_table(table_t1_1, {{1, 2}, {10, 20}}) ? success : false;

    // select * from test_lock.dbo.test where id in (1,2); -- T2
    auto tbl_id_t2 = t2.get_table_id(path_snap2);
    auto table_t2_1 = t2.read_table(tbl_id_t2);
    success = check_table(table_t2_1, {{1, 2}, {10, 20}}) ? success : false;

    // update test_lock.dbo.test set value = 11 where id = 1; -- T1.
    update(t1, path_snap2, 1, 11);

    // update test_lock.dbo.test set value = 21 where id = 2; -- T2.
    update(t2, path_snap2, 2, 21);

    t1.commit();
    versions = check_consistency(config2, levels, versions);

    t2.commit();
    versions = check_consistency(config2, levels, versions);
  }
  Aws::ShutdownAPI(options);
  return success;
}

bool hermitage_g2(std::vector<bool> levels, LHConfig::LvSettings& settings) {
  bool success = true;
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "------ G2 ------" << std::endl;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    std::string path_lock = TEST_LOCK;
    std::string path_snap1 = TEST_SNAP1;
    std::string path_snap2 = TEST_SNAP2;

    auto versions = check_consistency(config2, levels, {}, true);
    versions[2] += 2;

    // T1
    LHTransactions::TransactionManagerGeneric t1(levels, path_snap2, config2,
                                                 1);
    t1.begin_transaction_ycsb();
    // T2
    LHTransactions::TransactionManagerGeneric t2(levels, path_snap2, config2,
                                                 2);
    t2.begin_transaction_ycsb();

    std::string tmp_string = "";
    // select * from test_lock.dbo.test where id in (1,2); -- T1
    auto tbl_id = t1.get_table_id(path_snap2);
    auto table_t1_1 = t1.read_table(tbl_id);
    success = check_table(table_t1_1, {{1, 2}, {11, 21}}) ? success : false;

    // select * from test_lock.dbo.test where id in (1,2); -- T2
    auto tbl_id_t2 = t2.get_table_id(path_snap2);
    auto table_t2_1 = t2.read_table(tbl_id_t2);
    success = check_table(table_t2_1, {{1, 2}, {11, 21}}) ? success : false;

    // insert into test_lock.dbo.test (id, value) values(3, 30); -- T1
    auto tmp = LHHelpers::encodeAsParquet({"id", "value"}, {3, 30});
    auto parquet_str = std::string(tmp->first, tmp->second);
    t1.add_file(tbl_id, parquet_str, "3");
    std::free(tmp->first);

    // insert into test_lock.dbo.test (id, value) values(4, 42); -- T2
    auto tmp2 = LHHelpers::encodeAsParquet({"id", "value"}, {4, 42});
    auto parquet_str2 = std::string(tmp2->first, tmp2->second);
    t2.add_file(tbl_id_t2, parquet_str2, "4");
    // std::free(tmp2.first);

    t1.commit();
    versions = check_consistency(config2, levels, versions);
    t2.commit();
    versions = check_consistency(config2, levels, versions);

    // select * from test_lock.dbo.test where value % 3 = 0;
    success = check_table(config2, path_snap2, {{1, 2, 3, 4}, {11, 21, 30, 42}})
                  ? success
                  : false;
  }
  Aws::ShutdownAPI(options);
  return success;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Please provide a config file and the level combination you "
                 "want to use"
              << std::endl;
    return 1;
  }

  std::string config_path = argv[1];
  LHConfig::LvSettings settings(config_path);
  if (!settings.parse()) {
    std::cerr << "invalid config" << std::endl;
    return 1;
  }

  std::vector<bool> levels = {false, false, false};

  auto lvl = std::string(argv[2]);

  for (auto l : lvl) {
    if (l == '0') {
      levels[0] = !levels[0];
    }

    if (l == '1') {
      levels[1] = !levels[1];
    }

    if (l == '2') {
      levels[2] = !levels[2];
    }
  }

  std::cout << "Welcome!\nStarting Hermitage using LakeVilla ";
  for (uint32_t i = 0; i < levels.size(); i++) {
    if (levels[i]) {
      std::cout << i;
    }
  }
  std::cout << std::endl;

  std::vector<bool (*)(LHTransactions::LakeVillaConf, LHConfig::LvSettings&)>
      funcs = {hermitage_g0,    hermitage_g1a, hermitage_g1b, hermitage_g1c,
               hermitage_otv,   hermitage_pmp, hermitage_p4,  hermitage_gsingle,
               hermitage_gitem, hermitage_g2};

  for (auto fnc : funcs) {
    if (fnc(levels, settings)) {
      std::cout << "success" << std::endl;
    } else {
      std::cout << "failure (manual comparison necessary)" << std::endl;
    }
  }

  return 0;
}
