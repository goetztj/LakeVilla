#include "lhtransactionsBanking.hpp"

#include <chrono>
#include <mutex>
#include <sstream>

#include "Connectors/MinIOConnector.hpp"

namespace {

using Clock = std::chrono::high_resolution_clock;

double ms_since(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}

// balance from the printed Arrow column, e.g. [["123.45"]]
std::optional<double> parse_balance(const std::string& column) {
  auto first = column.find_first_of('"');
  auto last = column.find_last_of('"');
  if (first == std::string::npos || last <= first) {
    return std::nullopt;
  }
  try {
    return std::stod(column.substr(first + 1, last - first - 1));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

void print_perf(const char* name, const std::vector<double>& values) {
  if (values.empty()) {
    return;
  }
  std::cout << "----- " << name << " -----" << std::endl;
  double sum = 0;
  for (auto& ref : values) {
    std::cout << ref << std::endl;
    sum += ref;
  }
  std::cout << "----------------" << std::endl;
  std::cout << name << " (avg): " << sum / values.size() << std::endl;
  std::cout << "----------------" << std::endl;
}

}  // namespace

ycsbc::LHTransactionsBankingDB::LHTransactionsBankingDB(
    std::string& config_path, std::vector<bool> levels, Layout layout,
    bool init_global_log)
    : levels(std::move(levels)),
      layout(layout),
      init_global_log(init_global_log) {
  this->settings = std::make_unique<LHConfig::LvSettings>(config_path);
  this->settings->parse();
  this->settings->apply_defaults();
}

void ycsbc::LHTransactionsBankingDB::Init() {
  auto config = this->settings->minio_config();

  next_actor = 0;

  this->table_schema = arrow::schema({arrow::field("Banking_ID", arrow::utf8()),
                                      arrow::field("balance", arrow::utf8())});

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

  this->manager0 =
      std::make_unique<LHTransactions::TransactionManagerDeltaLake>(
          levels, path0, config, LHTransactions::unique_transaction_id());
  if (layout != Layout::Query) {
    this->manager1 =
        std::make_unique<LHTransactions::TransactionManagerDeltaLake>(
            levels, path1, config, LHTransactions::unique_transaction_id());
  }

  if (init_global_log) {
    // the global version log is seeded once, not by every client thread
    static std::once_flag seeded;
    std::call_once(seeded, [&] {
      std::stringstream s;
      s << path0 << " 1" << std::endl << path1 << " 1" << std::endl;
      auto log = s.str();
      StorageConnector::MinIOConnector conn(&config);
      if (!conn.write(this->manager0->global_log_path, log.c_str(),
                      log.size())) {
        std::cerr << "unable to seed the global log" << std::endl;
      }
    });
  }
}

void ycsbc::LHTransactionsBankingDB::Close() {
  this->manager0 = nullptr;
  this->manager1 = nullptr;

  std::cout << "changes: " << this->changes0 << "; " << this->changes1
            << std::endl;

  if (!this->defer_stats) {
    this->print_stats();
  }
}

ycsbc::LHTransactionsBankingDB::Account ycsbc::LHTransactionsBankingDB::actor(
    int id) {
  double* changes = id == 0 ? &this->changes0 : &this->changes1;
  if (layout == Layout::Query) {
    return {this->manager0.get(), 0, std::to_string(id), changes};
  }
  return {id == 0 ? this->manager0.get() : this->manager1.get(), 0, "0",
          changes};
}

bool ycsbc::LHTransactionsBankingDB::begin(
    LHTransactions::TransactionManager* manager) {
  auto start = Clock::now();
  bool started = manager->begin_transaction_ycsb();
  this->init_perf.push_back(ms_since(start));
  return started;
}

bool ycsbc::LHTransactionsBankingDB::commit(
    LHTransactions::TransactionManager* manager) {
  auto start = Clock::now();
  bool committed = manager->commit();
  this->commit_perf.push_back(ms_since(start));
  return committed;
}

std::optional<double> ycsbc::LHTransactionsBankingDB::read_balance(
    Account& account, std::string& path) {
  std::vector<std::string> fields = {"balance"};
  std::vector<ycsbc::DB::KVPair> result;
  auto found = account.manager->read_file(account.table_id, account.key,
                                          &fields, result, path);
  if (found.empty() || result.empty()) {
    std::cerr << "account " << account.key << " not found" << std::endl;
    return std::nullopt;
  }
  return parse_balance(result[0].second);
}

bool ycsbc::LHTransactionsBankingDB::write_balance(
    Account& account, double balance, LHTransactions::UpdateOperations op,
    double change, std::string& path) {
  std::vector<ycsbc::DB::KVPair> update_pair = {
      {"balance", std::to_string(balance)}};
  std::vector<ycsbc::DB::KVPair> op_pair = {{"balance", std::to_string(change)}};
  auto prev_id =
      account.manager->register_update_pairs(update_pair, op, op_pair);
  account.manager->remove_file(account.table_id, account.key, &path, prev_id);

  auto table = this->arrow_table_builder(account.key, std::to_string(balance));
  return account.manager->add_file(account.table_id, table, account.key,
                                   prev_id);
}

// check balance
int ycsbc::LHTransactionsBankingDB::Read(
    const std::string& table, const std::string& key,
    const std::vector<std::string>* fields,
    std::vector<ycsbc::DB::KVPair>& result) {
  auto account = this->actor(this->next_actor);
  this->next_actor = 1 - this->next_actor;

  if (!this->begin(account.manager)) {
    return 1;
  }

  std::string path = "";
  std::vector<std::string> account_fields = {"balance"};
  auto start = Clock::now();
  auto success = account.manager->read_file(account.table_id, account.key,
                                            &account_fields, result, path);
  this->read_perf.push_back(ms_since(start));

  bool committed = this->commit(account.manager);
  this->commit_diff.push_back(0.0);

  return success.empty() || !committed ? 1 : 0;
}

// withdraw
int ycsbc::LHTransactionsBankingDB::Scan(
    const std::string& table, const std::string& key, int len,
    const std::vector<std::string>* fields,
    std::vector<std::vector<ycsbc::DB::KVPair>>& result) {
  auto account = this->actor(this->next_actor);
  this->next_actor = 1 - this->next_actor;

  if (!this->begin(account.manager)) {
    return 1;
  }

  std::string path = "";
  auto start = Clock::now();
  auto balance = this->read_balance(account, path);
  bool written = false;
  double change = 0.01 * (std::rand() % 50001);
  if (balance) {
    written = this->write_balance(account, *balance - change,
                                  LHTransactions::UpdateOperations::SUB,
                                  change, path);
  }
  this->scan_perf.push_back(ms_since(start));

  bool committed = this->commit(account.manager);
  this->commit_diff.push_back(0.0);

  if (!balance || !written || !committed) {
    return 1;
  }
  *account.changes -= change;
  return 0;
}

// transfer
int ycsbc::LHTransactionsBankingDB::Update(
    const std::string& table, const std::string& key,
    std::vector<ycsbc::DB::KVPair>& values) {
  int sender_id = this->next_actor;
  this->next_actor = 1 - this->next_actor;
  auto sender = this->actor(sender_id);
  auto receiver = this->actor(1 - sender_id);

  double change = 0.01 * (std::rand() % 50001);

  if (layout == Layout::SeparateTables) {
    // withdraw and deposit are separate transactions on the two tables
    if (!this->begin(sender.manager)) {
      return 1;
    }
    std::string path = "";
    auto start = Clock::now();
    auto balance = this->read_balance(sender, path);
    bool written = balance && this->write_balance(
                                  sender, *balance - change,
                                  LHTransactions::UpdateOperations::SUB,
                                  change, path);
    this->scan_perf.push_back(ms_since(start));
    bool committed = this->commit(sender.manager);
    if (!written || !committed) {
      return 1;
    }
    *sender.changes -= change;

    auto commit_diff_start = Clock::now();
    if (!this->begin(receiver.manager)) {
      return 1;
    }
    path = "";
    auto start2 = Clock::now();
    auto balance2 = this->read_balance(receiver, path);
    bool written2 = balance2 && this->write_balance(
                                    receiver, *balance2 + change,
                                    LHTransactions::UpdateOperations::ADD,
                                    change, path);
    this->scan_perf.push_back(ms_since(start2));
    bool committed2 = this->commit(receiver.manager);
    this->commit_diff.push_back(ms_since(commit_diff_start));
    if (!written2 || !committed2) {
      // the money left the sender but did not arrive (no multi-table
      // transaction in this layout)
      return 1;
    }
    *receiver.changes += change;
    return 0;
  }

  // one transaction for both accounts
  if (!this->begin(sender.manager)) {
    return 1;
  }
  if (layout == Layout::MultiTable) {
    auto& other = sender_id == 0 ? this->settings->pathToBankingTable1
                                 : this->settings->pathToBankingTable0;
    if (!sender.manager->open_new_table(other)) {
      this->commit(sender.manager);
      return 1;
    }
    receiver.manager = sender.manager;
    receiver.table_id = sender.manager->get_table_id(other);
  }

  std::string path = "";
  auto start = Clock::now();
  auto balance = this->read_balance(sender, path);
  bool written = balance && this->write_balance(
                                sender, *balance - change,
                                LHTransactions::UpdateOperations::SUB, change,
                                path);
  this->scan_perf.push_back(ms_since(start));

  path = "";
  auto start2 = Clock::now();
  auto balance2 = written ? this->read_balance(receiver, path) : std::nullopt;
  bool written2 = balance2 && this->write_balance(
                                  receiver, *balance2 + change,
                                  LHTransactions::UpdateOperations::ADD,
                                  change, path);
  this->scan_perf.push_back(ms_since(start2));

  if (!written2) {
    // never commit half of a transfer
    sender.manager->abort();
    return 1;
  }

  bool committed = this->commit(sender.manager);
  this->commit_diff.push_back(0.0);
  if (!committed) {
    return 1;
  }
  *sender.changes -= change;
  *receiver.changes += change;
  return 0;
}

int ycsbc::LHTransactionsBankingDB::Insert(
    const std::string& table, const std::string& key,
    std::vector<ycsbc::DB::KVPair>& values) {
  return 0;
}

int ycsbc::LHTransactionsBankingDB::Delete(const std::string& table,
                                           const std::string& key) {
  return 0;
}

std::shared_ptr<arrow::Table>
ycsbc::LHTransactionsBankingDB::arrow_table_builder(
    const std::string& key, const std::string& balance) {
  arrow::StringBuilder key_builder, balance_builder;
  PARQUET_THROW_NOT_OK(key_builder.Append(key));
  PARQUET_THROW_NOT_OK(balance_builder.Append(balance));
  std::shared_ptr<arrow::Array> key_array, balance_array;
  PARQUET_THROW_NOT_OK(key_builder.Finish(&key_array));
  PARQUET_THROW_NOT_OK(balance_builder.Finish(&balance_array));
  return arrow::Table::Make(this->table_schema, {key_array, balance_array}, 1);
}

void ycsbc::LHTransactionsBankingDB::print_stats() {
  if (!this->init_perf.empty()) {
    std::cout << "Init: " << this->init_perf[0] << "ms" << std::endl;
  }
  print_perf("read", this->read_perf);
  print_perf("insert", this->insert_perf);
  print_perf("update", this->update_perf);
  if (!this->commit_perf.empty()) {
    std::cout << "Commit: " << this->commit_perf[0] << "ms" << std::endl;
  }
}
