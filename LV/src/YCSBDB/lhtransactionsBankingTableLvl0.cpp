#include "lhtransactionsBankingTableLvl0.hpp"

std::atomic<uint32_t> ycsbc::LHTransactionsBankingTableDBLvl0::next_id(0);

ycsbc::LHTransactionsBankingTableDBLvl0::LHTransactionsBankingTableDBLvl0(
    std::string& config_path) {
  this->settings = std::make_unique<LHConfig::LvSettings>(config_path);

  this->settings->parse();
}

void ycsbc::LHTransactionsBankingTableDBLvl0::Init() {
  StorageConnector::MinIOConfig config2;
  config2.endpointURI = this->settings->host;
  config2.user = this->settings->user;
  config2.passwd = this->settings->passwd;
  config2.region = "";
  config2.connectTimeout = 60000;
  config2.requestTimeout = 10000;
  config2.default_bucket = "warehouse";

  next_actor = 0;

  arrow::FieldVector attributes;

  attributes.push_back(arrow::field("Banking_ID", arrow::utf8()));
  attributes.push_back(arrow::field("balance", arrow::utf8()));

  this->table_schema = arrow::schema(attributes);

  this->read_perf = {};
  this->insert_perf = {};
  this->update_perf = {};
  this->init_perf = {};
  this->commit_perf = {};
  this->scan_perf = {};
  this->commit_diff = {};

  this->changes0 = 0;
  this->changes1 = 0;

  std::string path0 = this->settings->pathToBankingTable0;
  std::string path1 = this->settings->pathToBankingTable1;

  uint32_t t_id = ycsbc::LHTransactionsBankingTableDBLvl0::next_id.fetch_add(1);

  std::vector<bool> config = {true, false, false};
  std::vector<bool> config_2 = {true, false, false};
  this->manager0 = std::make_unique<LHTransactions::TransactionManagerGeneric>(
      config, path0, config2, t_id);
  this->manager1 = std::make_unique<LHTransactions::TransactionManagerGeneric>(
      config_2, path1, config2, t_id + 1);
}

void ycsbc::LHTransactionsBankingTableDBLvl0::Close() {
  this->manager0 = nullptr;
  this->manager1 = nullptr;

  std::cout << "changes: " << this->changes0 << "; " << this->changes1
            << std::endl;
}

// check balance
int ycsbc::LHTransactionsBankingTableDBLvl0::Read(
    const std::string& table, const std::string& key,
    const std::vector<std::string>* fields,
    std::vector<ycsbc::DB::KVPair>& result) {
  std::string path = "";

  LHTransactions::TransactionManager* manager = nullptr;
  if (this->next_actor == 0) {
    manager = this->manager0.get();
    this->next_actor = 1;
  } else {
    manager = this->manager1.get();
    this->next_actor = 0;
  }

  auto c_start = std::chrono::high_resolution_clock::now();
  manager->begin_transaction_ycsb();
  auto c_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> c_duration = c_end - c_start;
  this->init_perf.push_back(c_duration.count());

  auto account_key = "0";
  std::vector<std::string> account_fields;
  account_fields.push_back("balance");
  auto start = std::chrono::high_resolution_clock::now();
  auto success =
      manager->read_file(0, account_key, &account_fields, result, path);
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration = end - start;
  this->read_perf.push_back(duration.count());

  auto e_start = std::chrono::high_resolution_clock::now();
  manager->commit();
  auto e_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> e_duration = e_end - e_start;
  this->commit_perf.push_back(e_duration.count());
  this->commit_diff.push_back(0.0);

  if (success.empty()) {
    return 1;
  }

  return 0;
}

// withdraw
int ycsbc::LHTransactionsBankingTableDBLvl0::Scan(
    const std::string& table, const std::string& key, int len,
    const std::vector<std::string>* fields,
    std::vector<std::vector<ycsbc::DB::KVPair>>& result) {
  LHTransactions::TransactionManager* manager = nullptr;
  double* changes_log = nullptr;
  if (this->next_actor == 0) {
    manager = this->manager0.get();
    changes_log = &(this->changes0);
    this->next_actor = 1;
  } else {
    manager = this->manager1.get();
    this->next_actor = 0;
    changes_log = &(this->changes1);
  }

  std::string path = "";
  auto c_start = std::chrono::high_resolution_clock::now();
  manager->begin_transaction_ycsb();
  auto c_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> c_duration = c_end - c_start;
  this->init_perf.push_back(c_duration.count());

  auto account_key = "0";
  std::vector<std::string> account_fields;
  account_fields.push_back("balance");
  std::vector<ycsbc::DB::KVPair> tmp_result;
  auto start = std::chrono::high_resolution_clock::now();
  auto success =
      manager->read_file(0, account_key, &account_fields, tmp_result, path);

  std::string t = tmp_result[0].second;
  t = t.substr(t.find_first_of('"') + 1, t.find_last_of('"'));
  double balance = std::stod(t);

  double changes = 0.01 * (std::rand() % 50001);

  balance -= changes;
  *changes_log -= changes;

  std::vector<ycsbc::DB::KVPair> update_pair;
  update_pair.push_back({"balance", std::to_string(balance)});
  std::vector<ycsbc::DB::KVPair> op_pair;
  op_pair.push_back({"balance", std::to_string(changes)});
  auto prev_id = manager->register_update_pairs(
      update_pair, LHTransactions::UpdateOperations::SUB, op_pair);
  manager->remove_file(0, account_key, &path, prev_id);
  std::vector<ycsbc::DB::KVPair> account_fields2;
  account_fields2.push_back({"balance", std::to_string(balance)});
  auto arrow_table = this->arrow_table_builder(account_key, account_fields2);
  bool add_success = manager->add_file(0, arrow_table, account_key, prev_id);

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration = end - start;
  this->scan_perf.push_back(duration.count());

  auto e_start = std::chrono::high_resolution_clock::now();
  manager->commit();
  auto e_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> e_duration = e_end - e_start;
  this->commit_perf.push_back(e_duration.count());
  this->commit_diff.push_back(0.0);

  if (success.empty()) {
    return 1;
  }

  return 0;
}

int ycsbc::LHTransactionsBankingTableDBLvl0::Update(
    const std::string& table, const std::string& key,
    std::vector<ycsbc::DB::KVPair>& values) {
  LHTransactions::TransactionManager* manager_sender = nullptr;
  LHTransactions::TransactionManager* manager_rec = nullptr;
  double* changes_log_sender = nullptr;
  double* changes_log_rec = nullptr;
  if (this->next_actor == 0) {
    manager_sender = this->manager0.get();
    manager_rec = this->manager1.get();
    this->next_actor = 1;
    changes_log_sender = &(this->changes0);
    changes_log_rec = &(this->changes1);
  } else {
    manager_sender = this->manager1.get();
    manager_rec = this->manager0.get();
    this->next_actor = 0;
    changes_log_sender = &(this->changes1);
    changes_log_rec = &(this->changes0);
  }

  std::string path = "";
  auto c_start = std::chrono::high_resolution_clock::now();
  manager_sender->begin_transaction_ycsb();
  auto c_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> c_duration = c_end - c_start;
  this->init_perf.push_back(c_duration.count());

  auto account_key = "0";
  std::vector<std::string> account_fields;
  account_fields.push_back("balance");
  std::vector<ycsbc::DB::KVPair> tmp_result;
  auto start = std::chrono::high_resolution_clock::now();
  auto success = manager_sender->read_file(0, account_key, &account_fields,
                                           tmp_result, path);

  std::string t = tmp_result[0].second;
  t = t.substr(t.find_first_of('"') + 1, t.find_last_of('"'));
  double balance = std::stod(t);

  double changes = 0.01 * (std::rand() % 50001);

  balance -= changes;
  (*changes_log_sender) -= changes;

  std::vector<ycsbc::DB::KVPair> update_pair;
  update_pair.push_back({"balance", std::to_string(balance)});
  std::vector<ycsbc::DB::KVPair> op_pair;
  op_pair.push_back({"balance", std::to_string(changes)});
  auto prev_id = manager_sender->register_update_pairs(
      update_pair, LHTransactions::UpdateOperations::SUB, op_pair);
  manager_sender->remove_file(0, account_key, &path, prev_id);

  std::vector<ycsbc::DB::KVPair> account_fields2;
  account_fields2.push_back({"balance", std::to_string(balance)});

  auto arrow_table = this->arrow_table_builder(account_key, account_fields2);
  bool add_success =
      manager_sender->add_file(0, arrow_table, account_key, prev_id);

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration = end - start;
  this->scan_perf.push_back(duration.count());

  auto e_start = std::chrono::high_resolution_clock::now();

  manager_sender->commit();
  auto e_end = std::chrono::high_resolution_clock::now();
  auto commit_diff_start = std::chrono::high_resolution_clock::now();

  /// -- get money

  path = "";
  auto c_start2 = std::chrono::high_resolution_clock::now();
  manager_rec->begin_transaction_ycsb();
  auto c_end2 = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> c_duration2 = c_end2 - c_start2;
  this->init_perf.push_back(c_duration2.count());

  account_key = "0";
  std::vector<std::string> account_fields3;
  account_fields3.push_back("balance");
  std::vector<ycsbc::DB::KVPair> tmp_result3;
  auto start2 = std::chrono::high_resolution_clock::now();
  auto success2 = manager_rec->read_file(0, account_key, &account_fields3,
                                         tmp_result3, path);

  std::string t2 = tmp_result3[0].second;
  t2 = t2.substr(t2.find_first_of('"') + 1, t2.find_last_of('"'));
  double balance2 = std::stod(t2);

  balance2 += changes;
  (*changes_log_rec) += changes;

  std::vector<ycsbc::DB::KVPair> update_pair2;
  update_pair2.push_back({"balance", std::to_string(balance2)});
  std::vector<ycsbc::DB::KVPair> op_pair2;
  op_pair2.push_back({"balance", std::to_string(changes)});
  auto prev_id2 = manager_rec->register_update_pairs(
      update_pair2, LHTransactions::UpdateOperations::ADD, op_pair2);
  manager_rec->remove_file(0, account_key, &path, prev_id2);

  std::vector<ycsbc::DB::KVPair> account_fields4;
  account_fields4.push_back({"balance", std::to_string(balance2)});

  auto arrow_table2 = this->arrow_table_builder(account_key, account_fields4);
  bool add_success2 =
      manager_rec->add_file(0, arrow_table2, account_key, prev_id2);

  auto end2 = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration2 = end2 - start2;
  this->scan_perf.push_back(duration2.count());

  auto e_start2 = std::chrono::high_resolution_clock::now();
  manager_rec->commit();
  auto e_end2 = std::chrono::high_resolution_clock::now();
  auto commit_diff_end = std::chrono::high_resolution_clock::now();

  std::chrono::duration<double, std::milli> e_duration2 = e_end2 - e_start2;
  this->commit_perf.push_back(e_duration2.count());

  std::chrono::duration<double, std::milli> commit_diff_duration =
      commit_diff_end - commit_diff_start;
  this->commit_diff.push_back(commit_diff_duration.count());

  if (success.empty()) {
    return 1;
  }

  return 0;
}

int ycsbc::LHTransactionsBankingTableDBLvl0::Insert(
    const std::string& table, const std::string& key,
    std::vector<ycsbc::DB::KVPair>& values) {
  return 0;
}

int ycsbc::LHTransactionsBankingTableDBLvl0::Delete(const std::string& table,
                                                    const std::string& key) {
  return 0;
}

std::shared_ptr<arrow::Table>
ycsbc::LHTransactionsBankingTableDBLvl0::arrow_table_builder(
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

void ycsbc::LHTransactionsBankingTableDBLvl0::print_stats() {
  std::cout << "Init: " << this->init_perf[0] << "ms" << std::endl;

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

  std::cout << "Commit: " << this->commit_perf[0] << "ms" << std::endl;
}
