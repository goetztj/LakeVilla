#include "Mixed.hpp"

LHLST::Mixed::Mixed(TpcdsPaths paths,
                    StorageConnector::MinIOConnector* connector,
                    uint32_t num_queries, uint32_t id,
                    preparedLSTContent* content, std::vector<bool> level_config)
    : Phase(paths, connector, num_queries, id, level_config) {
  this->txnmanager = nullptr;

  this->content = content;
}

void LHLST::Mixed::run(std::vector<double>& times) {
  std::random_device s;
  std::mt19937 generator{s()};

  std::uniform_int_distribution<> distr{1, 100};

  for (int i = 0; i < this->num_queries; i++) {
    auto number2 = distr(generator);
    std::string content = "";
    std::string table = "";
    std::stringstream stream;
    stream << this->id << "-" << i;
    std::string key = stream.str();

    auto number = distr(generator);

    if (number > 0 && number <= 10) {
      table = this->paths.customer_address;
      content = this->content->customer_address;
    }

    if (number > 10 && number <= 20) {
      table = this->paths.date_dim;
      content = this->content->date_dim;
    }

    if (number > 20 && number <= 30) {
      table = this->paths.household_demographics;
      content = this->content->household_demographics;
    }

    if (number > 30 && number <= 40) {
      table = this->paths.item;
      content = this->content->item;
    }

    if (number > 40 && number <= 50) {
      table = this->paths.reason;
      content = this->content->reason;
    }

    if (number > 50 && number <= 60) {
      table = this->paths.store;
      content = this->content->store;
    }

    if (number > 60 && number <= 70) {
      table = this->paths.store_sales;
      content = this->content->store_sales;
    }

    if (number > 70 && number <= 80) {
      table = this->paths.time_dim;
      content = this->content->time_dim;
    }

    if (number > 80 && number <= 90) {
      table = this->paths.web_page;
      content = this->content->web_page;
    }

    if (number > 90 && number <= 100) {
      table = this->paths.web_sales;
      content = this->content->web_sales;
    }

    auto start = std::chrono::high_resolution_clock::now();
    this->txnmanager =
        std::make_unique<LHTransactions::TransactionManagerGeneric>(
            level_config, table, connector, id);
    this->txnmanager->begin_transaction_ycsb();

    this->rTable(table);

    this->iRow(key, table, content);

    this->txnmanager->commit();
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> duration = end - start;

    times.push_back(duration.count());
  }
}

void LHLST::Mixed::run_single_table(std::vector<double>& times) {
  this->txnmanager->begin_transaction_ycsb();

  std::random_device s;
  std::mt19937 generator{s()};

  std::uniform_int_distribution<> distr{1, 100};

  std::string content = "";
  std::string table = "";

  auto number = distr(generator);

  if (number > 0 && number <= 10) {
    table = this->paths.customer_address;
    content = this->content->customer_address;
  }

  if (number > 10 && number <= 20) {
    table = this->paths.date_dim;
    content = this->content->date_dim;
  }

  if (number > 20 && number <= 30) {
    table = this->paths.household_demographics;
    content = this->content->household_demographics;
  }

  if (number > 30 && number <= 40) {
    table = this->paths.item;
    content = this->content->item;
  }

  if (number > 40 && number <= 50) {
    table = this->paths.reason;
    content = this->content->reason;
  }

  if (number > 50 && number <= 60) {
    table = this->paths.store;
    content = this->content->store;
  }

  if (number > 60 && number <= 70) {
    table = this->paths.store_sales;
    content = this->content->store_sales;
  }

  if (number > 70 && number <= 80) {
    table = this->paths.time_dim;
    content = this->content->time_dim;
  }

  if (number > 80 && number <= 90) {
    table = this->paths.web_page;
    content = this->content->web_page;
  }

  if (number > 90 && number <= 100) {
    table = this->paths.web_sales;
    content = this->content->web_sales;
  }

  for (int i = 0; i < this->num_queries; i++) {
    auto number2 = distr(generator);
    std::stringstream stream;
    stream << this->id << "-" << i;
    std::string key = stream.str();

    auto start = std::chrono::high_resolution_clock::now();
    if (number2 < 60) {
      // read
      this->rTable(table);
    } else if (number2 < 90) {
      // write (insert)
      this->iRow(key, table, content);
    } else {
      this->uTable(table);
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> duration = end - start;

    times.push_back(duration.count());
  }

  this->txnmanager->commit(false);
}

bool LHLST::Mixed::iRow(std::string key, std::string& table,
                        std::string& content) {
  auto tbl_id = txnmanager->get_table_id(table);
  if (tbl_id == UINT32_MAX) {
    txnmanager->open_new_table(table);
    tbl_id = txnmanager->get_table_id(table);
  }
  return txnmanager->add_file(tbl_id, content, key);
}

bool LHLST::Mixed::uTable(std::string& tbl_path) {
  auto tbl_id = txnmanager->get_table_id(tbl_path);
  if (tbl_id == UINT32_MAX) {
    txnmanager->open_new_table(tbl_path);
    tbl_id = txnmanager->get_table_id(tbl_path);
  }
  std::string path;
  txnmanager->read_random_file_simple(tbl_id, path);

  try {
    if (!path.empty()) {
      txnmanager->remove_file(tbl_id, path);
      txnmanager->add_file(tbl_id, path, true);
    }
  } catch (...) {
    std::cerr << "insert failed" << std::endl;
    return false;
  }

  return true;
}

bool LHLST::Mixed::rTable(std::string& tbl_path) {
  auto tbl_id = txnmanager->get_table_id(tbl_path);
  if (tbl_id == UINT32_MAX) {
    txnmanager->open_new_table(tbl_path);
    tbl_id = txnmanager->get_table_id(tbl_path);
  }
  std::string path;
  txnmanager->read_random_file_simple(tbl_id, path);
  return true;
}