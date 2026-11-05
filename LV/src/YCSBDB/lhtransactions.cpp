#include "lhtransactions.hpp"

#include <strings.h>

ycsbc::LHTransactionsDB::LHTransactionsDB(std::string& config_path, std::vector<bool> config, bool single) : config(std::move(config)), single_txn(single) {
  this->settings = std::make_unique<LHConfig::LvSettings>(config_path);

  this->settings->parse();
  this->settings->apply_defaults();

  this->read_only = true;
}

void ycsbc::LHTransactionsDB::Init() {
  StorageConnector::MinIOConfig config2 = this->settings->minio_config();

  this->read_only = true;

  arrow::FieldVector attributes;

  attributes.push_back(arrow::field("YCSB_KEY", arrow::utf8()));
  attributes.push_back(arrow::field("field0", arrow::utf8()));
  attributes.push_back(arrow::field("field1", arrow::utf8()));
  attributes.push_back(arrow::field("field2", arrow::utf8()));
  attributes.push_back(arrow::field("field3", arrow::utf8()));
  attributes.push_back(arrow::field("field4", arrow::utf8()));
  attributes.push_back(arrow::field("field5", arrow::utf8()));
  attributes.push_back(arrow::field("field6", arrow::utf8()));
  attributes.push_back(arrow::field("field7", arrow::utf8()));
  attributes.push_back(arrow::field("field8", arrow::utf8()));
  attributes.push_back(arrow::field("field9", arrow::utf8()));

  this->table_schema = arrow::schema(attributes);

  this->read_perf = {};
  this->insert_perf = {};
  this->update_perf = {};
  this->init_perf = {};
  this->commit_perf = {};

  std::string path = this->settings->pathToYCSBTable;

  uint32_t t_id = LHTransactions::unique_transaction_id();

  //std::vector<bool> config = {false, true, false};
  this->manager = std::make_unique<LHTransactions::TransactionManagerDeltaLake>(
      config, path, config2, t_id);

  if(single_txn){
    auto start = std::chrono::high_resolution_clock::now();
    this->manager->begin_transaction_ycsb();
    auto init_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = init_end - start;
    this->init_perf.push_back(duration.count());
  }
}

void ycsbc::LHTransactionsDB::Close() {
  std::cerr << "<<<<< redos: " << this->manager->times_redo << " >>>>>"
            << std::endl;
  if(single_txn){
    auto start = std::chrono::high_resolution_clock::now();
    this->manager->commit(this->read_only);
    auto commit_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = commit_end - start;
    this->commit_perf.push_back(duration.count());
  }

  this->manager = nullptr;

  if (!this->defer_stats) {
    this->print_stats();
  }
}

bool ycsbc::LHTransactionsDB::commit_operation(bool read_only) {
  auto op_end = std::chrono::high_resolution_clock::now();
  bool committed = this->manager->commit(read_only);
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration_c = end - op_end;
  this->commit_perf.push_back(duration_c.count());
  return committed;
}

int ycsbc::LHTransactionsDB::Read(const std::string& table,
                                      const std::string& key,
                                      const std::vector<std::string>* fields,
                                      std::vector<ycsbc::DB::KVPair>& result) {
  std::string path = "";

  if(!single_txn){
    auto start = std::chrono::high_resolution_clock::now();
    this->manager->begin_transaction_ycsb();
    auto b_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration_i = b_end - start;
    this->init_perf.push_back(duration_i.count());
  }
  
  auto init_end = std::chrono::high_resolution_clock::now();
  auto success = this->manager->read_file(0, key, fields, result, path);
  auto op_end = std::chrono::high_resolution_clock::now();

  if (!single_txn) {
    this->commit_operation(true);
  }

  std::chrono::duration<double, std::milli> duration = op_end - init_end;
  this->read_perf.push_back(duration.count());

  if (success.empty()) {
    return 1;
  }

  return 0;
}

int ycsbc::LHTransactionsDB::Scan(
    const std::string& table, const std::string&, int,
    const std::vector<std::string>*,
    std::vector<std::vector<ycsbc::DB::KVPair>>&) {
  std::string path = "";

  if(!single_txn){
    auto start = std::chrono::high_resolution_clock::now();
    this->manager->begin_transaction_ycsb();
    auto b_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration_i = b_end - start;
    this->init_perf.push_back(duration_i.count());
  }
  
  auto init_end = std::chrono::high_resolution_clock::now();
  //auto success = this->manager->read_file(0, key, fields, result, path);
  auto result_ptr = this->manager->read_table(0);
  auto op_end = std::chrono::high_resolution_clock::now();

  if (!single_txn) {
    this->commit_operation(true);
  }

  std::chrono::duration<double, std::milli> duration = op_end - init_end;
  this->read_perf.push_back(duration.count());

  if (!result_ptr) {
    return 1;
  }

  return 0;
}

int ycsbc::LHTransactionsDB::Update(
    const std::string& table, const std::string& key,
    std::vector<ycsbc::DB::KVPair>& values) {
  this->read_only = false;
  std::vector<ycsbc::DB::KVPair> old_values;
  std::string path = "";

  if(!single_txn){
    auto start = std::chrono::high_resolution_clock::now();
    this->manager->begin_transaction_ycsb();
    auto b_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration_i = b_end - start;
    this->init_perf.push_back(duration_i.count());
  }
  auto init_end = std::chrono::high_resolution_clock::now();
  auto arrowBaseTable = this->manager->read_file_as_table(0, key, path);

  if (!arrowBaseTable) {
    if (!single_txn) {
      // close the transaction of this operation
      this->commit_operation(true);
    }
    return 1;
  }

  auto prev_id = this->manager->register_update_pairs(
      values, LHTransactions::UpdateOperations::REP, values);

  std::thread sub_remove_thread([this, &key, &path, &prev_id] {
    this->manager->remove_file(0, key, &path, prev_id);
  });

  for (auto& ref : values) {
    auto col_names = arrowBaseTable->ColumnNames();
    for (int i = 0; i < col_names.size(); i++) {
      // the file has the column names of the table schema (Spark: FIELD0),
      // Delta column names are case-insensitive
      if (strcasecmp(col_names[i].c_str(), ref.first.c_str()) == 0) {
        auto field =
            std::make_shared<arrow::Field>(col_names[i], arrow::utf8());

        arrow::StringBuilder builder;
        PARQUET_THROW_NOT_OK(builder.Append(ref.second));
        std::shared_ptr<arrow::Array> arrow_array;
        std::vector<std::shared_ptr<arrow::Array>> chunks;

        PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));
        chunks.push_back(std::move(arrow_array));

        auto new_table = arrowBaseTable->SetColumn(
            i, field, std::make_shared<arrow::ChunkedArray>(std::move(chunks)));

        arrowBaseTable = *new_table;
      }
    }
  }

  bool add_success = this->manager->add_file(0, arrowBaseTable, key, prev_id);
  sub_remove_thread.join();
  auto op_end = std::chrono::high_resolution_clock::now();
  bool committed = true;
  if (!single_txn) {
    committed = this->commit_operation(false);
  }
  std::chrono::duration<double, std::milli> duration = op_end - init_end;
  this->update_perf.push_back(duration.count());

  if (add_success && committed) {
    return 0;
  }

  return 1;
}

int ycsbc::LHTransactionsDB::Insert(
    const std::string& table, const std::string& key,
    std::vector<ycsbc::DB::KVPair>& values) {
  this->read_only = false;
  if(!single_txn){
    auto start = std::chrono::high_resolution_clock::now();
    this->manager->begin_transaction_ycsb();
    auto b_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration_i = b_end - start;
    this->init_perf.push_back(duration_i.count());
  }
  auto init_end = std::chrono::high_resolution_clock::now();
  auto arrowTable = this->arrow_table_builder(key, values);

  bool add_success = this->manager->add_file(0, arrowTable, key);
  auto op_end = std::chrono::high_resolution_clock::now();
  bool committed = true;
  if (!single_txn) {
    committed = this->commit_operation(false);
  }
  std::chrono::duration<double, std::milli> duration = op_end - init_end;
  this->insert_perf.push_back(duration.count());

  if (add_success && committed) {
    return 0;
  }
  return 1;
}

int ycsbc::LHTransactionsDB::Delete(const std::string& table,
                                        const std::string& key) {
  this->read_only = false;
  if(!single_txn){
    auto start = std::chrono::high_resolution_clock::now();
    this->manager->begin_transaction_ycsb();
    auto b_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration_i = b_end - start;
    this->init_perf.push_back(duration_i.count());
  }
  auto success = this->manager->remove_file(0, key);
  auto op_end = std::chrono::high_resolution_clock::now();
  bool committed = true;
  if (!single_txn) {
    committed = this->commit_operation(false);
  }
  if (!success || !committed) {
    return 1;
  }
  return 0;
}

std::shared_ptr<arrow::Table> ycsbc::LHTransactionsDB::arrow_table_builder(
    const std::string& key, std::vector<ycsbc::DB::KVPair>& values,
    bool addKey) {
  arrow::ArrayVector value_vec;

  if (addKey) {
    arrow::StringBuilder builder_key;
    PARQUET_THROW_NOT_OK(builder_key.Append(key));
    std::shared_ptr<arrow::Array> arrow_array_key;

    PARQUET_THROW_NOT_OK(builder_key.Finish(&arrow_array_key));

    value_vec.push_back(arrow_array_key);
  }

  for (auto& entry : values) {
    arrow::StringBuilder builder;
    PARQUET_THROW_NOT_OK(builder.Append(entry.second));
    std::shared_ptr<arrow::Array> arrow_array;

    PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));

    value_vec.push_back(arrow_array);
  }

  return arrow::Table::Make(this->table_schema, value_vec, 1);
}

void ycsbc::LHTransactionsDB::print_stats() {
  if (!this->read_perf.empty()) {
    std::cout << "----- read -----" << std::endl;
    double sum = 0;
    for (auto& ref : this->read_perf) {
      std::cout << ref << std::endl;
      sum += ref;
    }
    std::cout << "----------------" << std::endl;
    std::cout << "read (avg): " << sum / this->read_perf.size() << std::endl;
    std::cout << "----------------" << std::endl;
  }

  if (!this->insert_perf.empty()) {
    std::cout << "----- insert -----" << std::endl;
    double sum = 0;
    for (auto& ref : this->insert_perf) {
      std::cout << ref << std::endl;
      sum += ref;
    }
    std::cout << "----------------" << std::endl;
    std::cout << "insert (avg): " << sum / this->insert_perf.size()
              << std::endl;
    std::cout << "----------------" << std::endl;
  }

  if (!this->update_perf.empty()) {
    std::cout << "----- update -----" << std::endl;
    double sum = 0;
    for (auto& ref : this->update_perf) {
      std::cout << ref << std::endl;
      sum += ref;
    }
    std::cout << "----------------" << std::endl;
    std::cout << "update (avg): " << sum / this->update_perf.size()
              << std::endl;
    std::cout << "----------------" << std::endl;
  }

  if (!this->init_perf.empty()) {
    std::cout << "----- init -----" << std::endl;
    double sum = 0;
    for (auto& ref : this->init_perf) {
      std::cout << ref << std::endl;
      sum += ref;
    }
    std::cout << "----------------" << std::endl;
    std::cout << "init (avg): " << sum / this->init_perf.size() << std::endl;
    std::cout << "----------------" << std::endl;
  }

  if (!this->commit_perf.empty()) {
    std::cout << "----- commit -----" << std::endl;
    double sum = 0;
    for (auto& ref : this->commit_perf) {
      std::cout << ref << std::endl;
      sum += ref;
    }
    std::cout << "----------------" << std::endl;
    std::cout << "commit (avg): " << sum / this->commit_perf.size()
              << std::endl;
    std::cout << "----------------" << std::endl;
  }
}
