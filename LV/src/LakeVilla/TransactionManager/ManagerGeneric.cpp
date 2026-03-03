#include "ManagerGeneric.hpp"

LHTransactions::TransactionManagerGeneric::TransactionManagerGeneric(
    LHTransactions::LakeVillaConf levels, std::string& path_to_table,
    StorageConnector::MinIOConfig config, uint32_t transaction_id)
    : TransactionManager(path_to_table, config, transaction_id) {
  this->connector = std::make_unique<StorageConnector::MinIOConnector>(&config);
  this->working_dir = "";
  this->tables.clear();
  this->next_step = 0;

  this->prev_markers = {};
  this->found = {"", ""};
  this->times_redo = 0;

  this->connector_ptr = this->connector.get();

  this->relation = std::make_unique<LHTransactions::GlobalRelation>();

  this->levels = levels;

  this->global_log_exists = true;

  while (this->levels.size() < 3) {
    this->levels.push_back(false);
  }

  this->explicit_log = true;
}

LHTransactions::TransactionManagerGeneric::TransactionManagerGeneric(
    LHTransactions::LakeVillaConf levels, std::string& path_to_table,
    StorageConnector::MinIOConnector* conn_ptr, uint32_t transaction_id)
    : TransactionManager(path_to_table, conn_ptr, transaction_id) {
  this->working_dir = "";
  this->tables.clear();
  this->next_step = 0;
  this->prev_markers = {};
  this->found = {"", ""};
  this->times_redo = 0;

  this->relation = std::make_unique<LHTransactions::GlobalRelation>();

  this->levels = levels;

  while (this->levels.size() < 3) {
    this->levels.push_back(false);
  }

  this->explicit_log = true;
}

LHTransactions::TransactionManagerGeneric::~TransactionManagerGeneric() {
  this->tables.clear();
  this->relation = nullptr;
}

bool LHTransactions::TransactionManagerGeneric::begin_transaction_ycsb() {
  if (!this->connector_ptr) {
    std::cerr << "no connector set" << std::endl;
    return false;
  }

  this->relation = std::make_unique<LHTransactions::GlobalRelation>();

  this->global_log_exists = true;

  this->working_dir = "";
  this->tables.clear();
  this->next_step = 0;
  this->explicit_log = true;
  this->prev_markers = {};
  this->found = {"", ""};
  this->times_redo = 0;
  ops.clear();
  double data_read;
  double data_write;
  this->update_vals.clear();
  this->update_ops.clear();

  this->prev_markers.clear();
  this->commit_diff.clear();

  this->local_log.clear();

  uint32_t retry_counter = 0;
  bool success = false;
  while (retry_counter < NUM_RETRIES) {
    if (this->begin(this->base_path)) {
      success = true;
      break;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_MILLISECONDS));
      retry_counter++;
    }
  }

  if (!success) {
    std::cout << "unable to execute 'begin transaction'" << std::endl;
    return false;
  }

  return true;
}

bool LHTransactions::TransactionManagerGeneric::begin_transaction() {
  uint32_t retry_counter = 0;
  bool success = false;
  while (retry_counter < NUM_RETRIES) {
    if (this->begin(this->base_path)) {
      success = true;
      break;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_MILLISECONDS));
      retry_counter++;
    }
  }

  if (!success) {
    return false;
  }

  bool end = false;

  while (!end) {
    std::cout << "Done! Now describe your next step" << std::endl
              << "[0] read a file" << std::endl
              << "[1] add a file" << std::endl
              << "[2] remove a file" << std::endl
              << "[3] commit" << std::endl
              << "[4] forced reroll" << std::endl
              << "[5] abort" << std::endl
              << "[6] open a table" << std::endl;
    std::string input;
    std::cin >> input;

    uint32_t number = std::stoi(input);

    bool result = false;

    switch (number) {
      case 0:
        result = this->read_file();
        break;
      case 1:
        result = this->add_file();
        break;
      case 2:
        result = this->remove_file();
        break;
      case 3:
        result = this->commit();
        end = true;
        break;
      case 4:
        result = this->reroll();
        break;
      case 5:
        result = this->abort();
        end = true;
        break;
      case 6:
        result = this->open_new_table();
        break;
      default:
        result = true;
        break;
    }

    if (!result) {
      if (this->reroll()) {
        // //std::cout << "success" << std::endl;
      } else {
        // //std::cout << "aborting transaction" << std::endl;
        return false;
      }
    }
  }

  return true;
}

LHTransactions::TableStats*
LHTransactions::TransactionManagerGeneric::select_table() {
  std::string in;
  std::cin >> in;

  uint32_t selection = std::stoi(in);

  if (selection >= 0 && selection < this->tables.size()) {
    return this->tables[selection].get();
  } else {
    return nullptr;
  }
}

void LHTransactions::TransactionManagerGeneric::read_table_simple(
    uint32_t table_id, uint32_t num_threads) {
  TableStats* table = this->tables[table_id].get();

  auto list = table->delta_log->get_list();

  std::vector<std::shared_ptr<arrow::Table>> partial_tables;
  std::vector<std::thread> threads;
  uint32_t req_threads = list.size() > num_threads ? num_threads : list.size();

  uint32_t regular_step = list.size() / req_threads;
  uint32_t special_steps = list.size() % req_threads;

  uint32_t last_read = 0;
  for (uint32_t i = 0; i < req_threads; i++) {
    uint32_t start = last_read;
    uint32_t end = last_read + regular_step;
    if (special_steps > 0) {
      end++;
      special_steps--;
    }
    last_read = end;

    threads.emplace_back([this, &list, start, end] {
      this->read_part_simple(list, start, end);
    });
  }

  for (auto& t : threads) {
    t.join();
  }
}

void LHTransactions::TransactionManagerGeneric::read_partial_table_simple(
    uint32_t table_id, uint32_t skip, uint32_t num_threads) {
  TableStats* table = this->tables[table_id].get();

  auto list = table->delta_log->get_list();

  std::vector<std::shared_ptr<arrow::Table>> partial_tables;
  std::vector<std::thread> threads;
  uint32_t req_threads = list.size() > num_threads ? num_threads : list.size();

  uint32_t regular_step = list.size() / req_threads;
  uint32_t special_steps = list.size() % req_threads;

  uint32_t last_read = 0;
  for (uint32_t i = 0; i < req_threads; i++) {
    uint32_t start = last_read;
    uint32_t end = last_read + regular_step;
    if (special_steps > 0) {
      end++;
      special_steps--;
    }
    last_read = end;

    threads.emplace_back([this, &list, start, end, skip] {
      this->read_part_simple_skip(list, start, end, skip);
    });
  }

  for (auto& t : threads) {
    t.join();
  }
}

void LHTransactions::TransactionManagerGeneric::head_table_simple(
    uint32_t table_id, uint32_t num_threads) {
  TableStats* table = this->tables[table_id].get();

  auto list = table->delta_log->get_list();

  std::vector<std::shared_ptr<arrow::Table>> partial_tables;
  std::vector<std::thread> threads;
  uint32_t req_threads = list.size() > num_threads ? num_threads : list.size();

  uint32_t regular_step = list.size() / req_threads;
  uint32_t special_steps = list.size() % req_threads;

  uint32_t last_read = 0;
  for (uint32_t i = 0; i < req_threads; i++) {
    uint32_t start = last_read;
    uint32_t end = last_read + regular_step;
    if (special_steps > 0) {
      end++;
      special_steps--;
    }
    last_read = end;

    threads.emplace_back([this, &list, start, end] {
      this->head_part_simple(list, start, end);
    });
  }

  for (auto& t : threads) {
    t.join();
  }
}

std::shared_ptr<arrow::Table>
LHTransactions::TransactionManagerGeneric::read_table(uint32_t table_id,
                                                      uint32_t num_threads) {
  TableStats* table = this->tables[table_id].get();

  auto list = table->delta_log->get_list();

  std::vector<std::shared_ptr<arrow::Table>> partial_tables;
  std::vector<std::thread> threads;
  uint32_t req_threads = list.size() > num_threads ? num_threads : list.size();

  uint32_t regular_step = list.size() / req_threads;
  uint32_t special_steps = list.size() % req_threads;

  uint32_t last_read = 0;
  partial_tables.resize(req_threads, nullptr);
  for (uint32_t i = 0; i < req_threads; i++) {
    uint32_t start = last_read;
    uint32_t end = last_read + regular_step;
    if (special_steps > 0) {
      end++;
      special_steps--;
    }
    last_read = end;

    threads.emplace_back([this, i, &partial_tables, &list, start, end] {
      this->read_part(partial_tables[i], list, start, end);
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  std::shared_ptr<arrow::Table> table_ptr = nullptr;

  for (auto& tbl : partial_tables) {
    if (table_ptr) {
      table_ptr = *(arrow::ConcatenateTables({table_ptr, tbl}));
    } else {
      table_ptr = tbl;
    }
  }

  return table_ptr;
}

void LHTransactions::TransactionManagerGeneric::read_random_file_simple(
    uint32_t table_id, std::string& path) {
  TableStats* table = this->tables[table_id].get();

  auto list = table->delta_log->get_list();

  if (!list.empty()) {
    std::random_device s;
    std::mt19937 generator{s()};

    int max = list.size() - 1;

    std::uniform_int_distribution<> distr{0, max};

    auto number = distr(generator);

    path = list[number];

    std::thread sub_thread([this, table, &path] {
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::READ,
                        *table, nullptr);
    });

    connector_ptr->read_mock(path);

    sub_thread.join();
  }
}

std::shared_ptr<arrow::Table>
LHTransactions::TransactionManagerGeneric::read_random_file_as_table(
    uint32_t table_id, std::string& path) {
  TableStats* table = this->tables[table_id].get();

  auto list = table->delta_log->get_list();

  if (!list.empty()) {
    std::random_device s;
    std::mt19937 generator{s()};

    int max = list.size() - 1;

    std::uniform_int_distribution<> distr{0, max};

    auto number = distr(generator);

    path = list[number];

    std::thread sub_thread([this, table, &path] {
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::READ,
                        *table, nullptr);
    });

    auto conn_result = connector_ptr->read2(path);

    std::shared_ptr<arrow::Table> table_ptr = nullptr;

    if (!conn_result->data) {
      table_ptr =
          LHHelpers::readParquetAsTable(conn_result->data, conn_result->length);
    }
    sub_thread.join();
    return table_ptr;
  } else {
    return nullptr;
  }
}

std::shared_ptr<arrow::Table>
LHTransactions::TransactionManagerGeneric::read_file_as_table(
    uint32_t table_id, const std::string& key, std::string& path) {
  TableStats* table = this->tables[table_id].get();

  path = table->delta_log->findFileWithStats(0, key);
  if (path.empty()) {
    return nullptr;
  }

  std::thread sub_thread([this, &path, table, &key] {
    this->add_substep(path, LHTransactions::TransactionManager::SubOpType::READ,
                      *table, &key);
  });

  auto conn_result = connector_ptr->read2(path);

  std::shared_ptr<arrow::Table> table_ptr = nullptr;

  if (conn_result->data) {
    table_ptr =
        LHHelpers::readParquetAsTable(conn_result->data, conn_result->length);
  }
  sub_thread.join();
  return table_ptr;
}

std::string LHTransactions::TransactionManagerGeneric::read_file(
    uint32_t table_id, const std::string& key,
    const std::vector<std::string>* fields,
    std::vector<std::pair<std::string, std::string>>& result,
    std::string& path) {
  TableStats* table = this->tables[table_id].get();

  path = table->delta_log->findFileWithStats(0, key);
  if (path.empty()) {
    return "";
  }

  std::thread sub_thread([this, &path, table, key] {
    this->add_substep(path, LHTransactions::TransactionManager::SubOpType::READ,
                      *table, &key);
  });

  auto conn_result = connector_ptr->read2(path);

  std::shared_ptr<arrow::Table> table_ptr = nullptr;

  if (conn_result->data) {
    table_ptr =
        LHHelpers::readParquetAsTable(conn_result->data, conn_result->length);
  } else {
    return "";
  }

  if (fields) {
    // only selected fields
    for (size_t i = 0; i < fields->size(); i++) {
      auto col = std::static_pointer_cast<arrow::StringArray>(
          table_ptr->column(i + 1)->chunk(0));
      std::string name = (*fields)[i];
      result.emplace_back(name, col->ToString());
    }
  } else {
    uint32_t counter = 0;
    for (auto& col : table_ptr->columns()) {
      auto col_cast =
          std::static_pointer_cast<arrow::StringArray>(col->chunk(0));
      result.emplace_back(table_ptr->ColumnNames()[counter],
                          col_cast->ToString());
      counter++;
    }
  }

  sub_thread.join();

  return path;
}

bool LHTransactions::TransactionManagerGeneric::read_file() {
  TableStats* table = nullptr;

  while (!table) {
    table = this->select_table();
  }

  auto file_list = table->delta_log->get_list();

  std::string in = "";
  std::cin >> in;

  uint32_t files_pos = std::stoi(in);

  std::thread sub_thread([this, &file_list, &files_pos, table] {
    this->add_substep(file_list[files_pos],
                      LHTransactions::TransactionManager::SubOpType::READ,
                      *table, nullptr);
  });

  auto result = connector_ptr->read2(file_list[files_pos]);

  if (result->data) {
    std::cout << LHHelpers::readParquet(result->data, result->length)
              << std::endl
              << "----------" << std::endl;
    table->in_bytes += result->length;
    sub_thread.join();
    return true;
  } else {
    sub_thread.join();
    return false;
  }
}

bool LHTransactions::TransactionManagerGeneric::add_file() {
  TableStats* table = nullptr;

  while (!table) {
    table = this->select_table();
  }

  auto arrowTable = LHHelpers::arrow_table_builder();

  auto encoded = LHHelpers::encodeAsParquet(arrowTable);
  std::string path;

  while (true) {
    std::stringstream stream;

    stream << table->base_path << "lhdata-" << std::to_string(std::rand())
           << ".parquet";

    if (!this->connector_ptr->check(stream.str())) {
      path = stream.str();
      break;
    }
  }

  if (!this->connector_ptr->write(path, encoded->first, encoded->second)) {
    return false;
  }

  auto stats = std::make_unique<FileStats>();
  stats->max_val = {"0", "0"};
  stats->min_val = {"0", "0"};
  stats->name = {"x", "y"};
  stats->path = path;
  stats->size = encoded->second;
  stats->valid = true;

  table->created_files.insert({path, std::move(stats)});

  table->created_files.find(path)->second->min_val = {"0", "0"};
  table->created_files.find(path)->second->max_val = {"0", "0"};
  if (table->deleted_files.count(path) != 0) {
    table->deleted_files.find(path)->second->valid = false;
  }

  std::thread sub_thread([this, &path, table] {
    this->add_substep(path, LHTransactions::TransactionManager::SubOpType::ADD,
                      *table, nullptr);
  });

  table->delta_log->register_operation(LHExecutor::LHDL::DLOperation::ADD, path,
                                       true);

  sub_thread.join();
  return true;
}

bool LHTransactions::TransactionManagerGeneric::add_file(uint32_t table_id,
                                                         std::string& path,
                                                         bool overwrite,
                                                         uint32_t prev) {
  TableStats* table = this->tables[table_id].get();

  std::thread sub_thread([this, &path, table, &prev] {
    if (prev != UINT32_MAX) {
      std::string key_str = std::to_string(prev);
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::ADD,
                        *table, &key_str);
    } else {
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::ADD,
                        *table, nullptr);
    }
  });

  if (this->explicit_log || prev == UINT32_MAX) {
    auto stats = std::make_unique<FileStats>();
    stats->min_val = {};
    stats->min_val = {};
    stats->name = {};
    stats->path = path;
    stats->size = UINT32_MAX;
    stats->valid = true;

    table->created_files.insert({path, std::move(stats)});

    table->created_files.find(path)->second->min_val = {};
    table->created_files.find(path)->second->max_val = {};

    if (table->deleted_files.count(path) != 0) {
      table->deleted_files.find(path)->second->valid = false;
    }

    std::vector<std::string> stat_keys = {};
    table->delta_log->register_operation(LHExecutor::LHDL::DLOperation::ADD,
                                         path, stat_keys, stat_keys, true,
                                         overwrite);
  }
  sub_thread.join();
  return true;
}

bool LHTransactions::TransactionManagerGeneric::add_file(
    uint32_t table_id, std::shared_ptr<arrow::Table> arrowTable,
    const std::string& key, uint32_t prev) {
  TableStats* table = this->tables[table_id].get();

  auto encoded = LHHelpers::encodeAsParquet(arrowTable);
  std::string path;

  while (true) {
    std::stringstream stream;

    stream << table->base_path << "lhdata-" << this->transaction_id << "-"
           << std::time(0) << "-" << this->next_step << ".parquet";

    if (!this->connector_ptr->check(stream.str())) {
      path = stream.str();
      break;
    }
  }

  std::thread sub_thread([this, &path, table, &prev] {
    if (prev != UINT32_MAX) {
      std::string key_str = std::to_string(prev);
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::ADD,
                        *table, &key_str);
    } else {
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::ADD,
                        *table, nullptr);
    }
  });

  if (this->explicit_log || prev == UINT32_MAX) {
    if (!this->connector_ptr->write(path, encoded->first, encoded->second)) {
      return false;
    }

    auto stats = std::make_unique<FileStats>();
    // FileStats stats;
    stats->min_val = {key, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
    stats->min_val = {key, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
    stats->name = {"YCSB_KEY", "field0", "field1", "field2", "field3", "field4",
                   "field5",   "field6", "field7", "field8", "field9"};
    stats->path = path;
    stats->size = encoded->second;
    stats->valid = true;

    table->created_files.insert({path, std::move(stats)});

    table->created_files.find(path)->second->min_val = {
        key, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
    table->created_files.find(path)->second->max_val = {
        key, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};

    if (table->deleted_files.count(path) != 0) {
      table->deleted_files.find(path)->second->valid = false;
    }

    std::vector<std::string> stat_keys = {key, "0", "0", "0", "0", "0",
                                          "0", "0", "0", "0", "0"};
    table->delta_log->register_operation(LHExecutor::LHDL::DLOperation::ADD,
                                         path, stat_keys, stat_keys, true);
  }
  sub_thread.join();
  return true;
}

bool LHTransactions::TransactionManagerGeneric::add_file(uint32_t table_id,
                                                         std::string& content,
                                                         const std::string& key,
                                                         uint32_t prev) {
  TableStats* table = this->tables[table_id].get();

  std::string path;

  while (true) {
    std::stringstream stream;

    stream << table->base_path << "lhdata-" << this->transaction_id << "-"
           << std::time(0) << "-" << this->next_step << ".parquet";

    if (!this->connector_ptr->check(stream.str())) {
      path = stream.str();
      break;
    }
  }

  std::thread sub_thread([this, &path, table, &prev] {
    if (prev != UINT32_MAX) {
      std::string key_str = std::to_string(prev);
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::ADD,
                        *table, &key_str);
    } else {
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::ADD,
                        *table, nullptr);
    }
  });

  if (this->explicit_log || prev == UINT32_MAX) {
    if (!this->connector_ptr->write(path, content.c_str(), content.size())) {
      return false;
    }

    auto stats = std::make_unique<FileStats>();
    // FileStats stats;
    stats->min_val = {key, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
    stats->min_val = {key, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
    stats->name = {"YCSB_KEY", "field0", "field1", "field2", "field3", "field4",
                   "field5",   "field6", "field7", "field8", "field9"};
    stats->path = path;
    stats->size = content.size();
    stats->valid = true;

    table->created_files.insert({path, std::move(stats)});

    table->created_files.find(path)->second->min_val = {
        key, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
    table->created_files.find(path)->second->max_val = {
        key, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};

    if (table->deleted_files.count(path) != 0) {
      table->deleted_files.find(path)->second->valid = false;
    }

    std::vector<std::string> stat_keys = {key, "0", "0", "0", "0", "0",
                                          "0", "0", "0", "0", "0"};
    table->delta_log->register_operation(LHExecutor::LHDL::DLOperation::ADD,
                                         path, stat_keys, stat_keys, true);
  }
  sub_thread.join();
  return true;
}

bool LHTransactions::TransactionManagerGeneric::remove_file() {
  TableStats* table = nullptr;

  while (!table) {
    table = this->select_table();
  }

  auto file_list = table->delta_log->get_list();

  std::string in = "";
  std::cin >> in;

  uint32_t file_pos = std::stoi(in);
  std::thread sub_thread([this, &file_list, &file_pos, table] {
    this->add_substep(file_list[file_pos],
                      LHTransactions::TransactionManager::SubOpType::DELETE,
                      *table, nullptr);
  });

  auto stats = std::make_unique<FileStats>();
  // FileStats stats;
  stats->max_val = {0, 0};
  stats->min_val = {0, 0};
  stats->name = {"x", "y"};
  stats->path = file_list[file_pos];
  stats->size = 0;
  stats->valid = true;

  table->deleted_files.insert({file_list[file_pos], std::move(stats)});

  table->deleted_files.find(file_list[file_pos])->second->min_val = {"0", "0"};
  table->deleted_files.find(file_list[file_pos])->second->max_val = {"0", "0"};

  if (table->created_files.count(file_list[file_pos]) != 0) {
    table->created_files.find(file_list[file_pos])->second->valid = false;
  }

  table->delta_log->register_operation(LHExecutor::LHDL::DLOperation::DELETE,
                                       file_list[file_pos], true);
  sub_thread.join();
  return true;
}

bool LHTransactions::TransactionManagerGeneric::remove_file(uint32_t table_id,
                                                            std::string& path,
                                                            uint32_t prev) {
  TableStats* table = this->tables[table_id].get();

  std::thread sub_thread([this, &path, table, &prev] {
    if (prev != UINT32_MAX) {
      std::string key_str = std::to_string(prev);
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::DELETE,
                        *table, &key_str);
    } else {
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::DELETE,
                        *table, nullptr);
    }
  });

  if (this->explicit_log) {
    auto stats = std::make_unique<FileStats>();
    // FileStats stats;
    stats->min_val = {};
    stats->min_val = {};
    stats->name = {};
    stats->path = path;
    stats->size = -1;
    stats->valid = true;
    table->deleted_files.insert({path, std::move(stats)});

    table->deleted_files.find(path)->second->min_val = {};
    table->deleted_files.find(path)->second->max_val = {};
    if (table->created_files.count(path) != 0) {
      table->created_files.find(path)->second->valid = false;
    }

    table->delta_log->register_operation(LHExecutor::LHDL::DLOperation::DELETE,
                                         path, true);
  }
  sub_thread.join();
  return true;
}

bool LHTransactions::TransactionManagerGeneric::remove_file(
    uint32_t table_id, const std::string& key, std::string* path_ptr,
    uint32_t prev) {
  TableStats* table = this->tables[table_id].get();
  std::string path = "";

  path = table->delta_log->findFileWithStats(0, key, true);

  std::thread sub_thread([this, &path, table, &prev] {
    if (prev != UINT32_MAX) {
      std::string key_str = std::to_string(prev);
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::DELETE,
                        *table, &key_str);
    } else {
      this->add_substep(path,
                        LHTransactions::TransactionManager::SubOpType::DELETE,
                        *table, nullptr);
    }
  });

  if (this->explicit_log) {
    auto stats = std::make_unique<FileStats>();
    stats->min_val = {key, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
    stats->min_val = {key, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
    stats->name = {"YCSB_KEY", "field0", "field1", "field2", "field3", "field4",
                   "field5",   "field6", "field7", "field8", "field9"};
    stats->path = path;
    stats->size = -1;
    stats->valid = true;

    table->deleted_files.insert({path, std::move(stats)});

    table->deleted_files.find(path)->second->min_val = {"0", "0"};
    table->deleted_files.find(path)->second->max_val = {"0", "0"};
    if (table->created_files.count(path) != 0) {
      table->created_files.find(path)->second->valid = false;
    }

    table->delta_log->register_operation(LHExecutor::LHDL::DLOperation::DELETE,
                                         path, true);
  }
  sub_thread.join();
  return true;
}

bool LHTransactions::TransactionManagerGeneric::wait_for_marker(
    std::string& path) {
  uint32_t counter = 0;
  while (true) {
    bool progress = true;
    auto content = this->connector_ptr->read2(path);
    std::string in(content->data, content->length);

    while (in.size() > 0) {
      auto pos = in.find_first_of('\n');

      std::string single_entry;
      if (pos != std::string_view::npos) {
        if (in.size() == 0) {
          std::cerr << "1" << std::endl;
        }
        single_entry = in.substr(0, pos);
      } else {
        single_entry = in;
        pos = in.size();
      }

      if (single_entry.size() > 1) {
        auto parsed = LHHelpers::readJSON_rapid(single_entry.c_str());
        if (parsed->HasMember("marker")) {
          progress = false;
          counter++;
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
          break;
        } else {
          auto table_path = path.substr(0, path.find("/_delta_log/") + 1);
          uint32_t table_id = 0;

          for (uint32_t i = 0; i < this->tables.size(); i++) {
            if (tables[i]->base_path.compare(table_path) == 0) {
              table_id = i;
              break;
            }
          }

          this->parseEntry(*parsed, tables[table_id]->delta_log.get());
          if (this->levels[2]) {
            auto pos_start = path.find("/_delta_log/") + 12;
            auto pos_end = path.find(".json") - 5;

            auto log_number = std::stoi(path.substr(pos_start, pos_end));
            this->local_log.find(table_path)->second = log_number;
          }
        }
      }

      if (pos + 1 < +in.size()) {
        if (in.size() == 0) {
          std::cerr << "2" << std::endl;
        }
        in = in.substr(pos + 1, in.size());
      } else {
        in = "";
      }
    }

    if (progress) {
      return true;
    } else if (counter >= 300) {
      break;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_MILLISECONDS));
    }
  }
  return false;
}

bool LHTransactions::TransactionManagerGeneric::commit(double& data_read,
                                                       double& data_write,
                                                       bool single_threaded) {
  data_read += 1.0 * this->connector_ptr->getReadsBytes() / 1e6;
  data_write += 1.0 * this->connector_ptr->getWritesBytes() / 1e6;
  return this->commit(single_threaded);
}

bool LHTransactions::TransactionManagerGeneric::commit(bool read_only,
                                                       bool single_threaded) {
  // if the hint "read_only" is set, we can just skip all marker checks and
  // redos and treat it internally as an abort
  if (read_only) {
    return abort();
  }

  bool valid = false;

  while (!valid) {
    if (this->levels[0] || this->levels[1]) {
      while (true) {
        bool exceeded = false;
        for (auto& marker : this->prev_markers) {
          if (this->connector_ptr->check(marker)) {
            if (!this->wait_for_marker(marker)) {
              exceeded = true;
              std::cerr << "exeeded for " << marker << std::endl;
              break;
            }
          }
        }
        if (!exceeded) {
          break;
        } else {
          std::cerr << "exceeded" << std::endl;
          sleep(this->transaction_id % 5);
          this->reopen_tables();
        }
      }
    }

    if (!this->explicit_log) {
      this->redo(1);
    }

    if ((!this->levels[0] && !this->levels[1] && !this->levels[2]) ||
        this->levels[0] || this->levels[1] || (this->levels[2])) {
      std::unordered_map<std::string, uint32_t> version_read_map;
      std::unordered_map<std::string, uint32_t> version_write_map;
      bool started_validation = false;
      std::string stage_content = "";

      if (this->start_validation(version_read_map, version_write_map,
                                 stage_content)) {
        for (auto& table : this->tables) {
          if (!table->created_files.empty() || !table->deleted_files.empty()) {
            std::string log_header = "";
            this->createHeaderCommit(log_header, *table);

            std::stringstream full_log;
            full_log << log_header;

            for (auto& file : table->created_files) {
              if (file.second->valid &&
                  (file.first.find("lhdata") != std::string::npos ||
                   file.first.find("part-") != std::string::npos)) {
                std::string entry = "";
                this->createOperationAddEntry(*(file.second), entry);

                full_log << std::endl << entry;
              }
            }

            for (auto& file : table->deleted_files) {
              if (file.second->valid &&
                  (file.first.find("lhdata") != std::string::npos ||
                   file.first.find("part-") != std::string::npos)) {
                std::string entry = "";
                this->createOperationDeleteEntry(*(file.second), entry);

                full_log << std::endl << entry;
              }
            }

            auto full_log_string = full_log.str();

            if (!this->levels[0] && !this->levels[1] && !this->levels[2]) {
              if (this->connector_ptr->check(table->marker->marker_path)) {
                // log already exists
                return false;
              }
            }

            this->connector_ptr->write(table->marker->marker_path,
                                       full_log_string.c_str(),
                                       full_log_string.size());

            table->marker->setInvalid();

            if (!this->levels[0] && !this->levels[1] && !this->levels[2]) {
              // check if log version was sucessfully claimed
              auto entry =
                  this->connector_ptr->read2(table->marker->marker_path);
              std::string content(entry->data, entry->length);
              if (full_log_string.compare(content) != 0) {
                // log already exists
                return false;
              }
            }
          }
        }

        this->create_global_snapshot(version_read_map, version_write_map,
                                     stage_content);
        valid = true;
      } else {
        this->connector_ptr->delete_file(PathToGlobalTmpLog);
        if (this->levels[0] && this->levels[2]) {
          std::cerr << "invalid for " << this->tables[0]->marker->marker_path
                    << std::endl;
          this->abort(false);
          this->reopen_tables();

        } else {
          this->abort(false);
          break;
        }
      }
    }
  }

  this->clear_subdirectory();
  return valid;
}

bool LHTransactions::TransactionManagerGeneric::commit(
    std::vector<double>& timestamps) {
  std::chrono::time_point<std::chrono::high_resolution_clock> start =
      std::chrono::high_resolution_clock::now();
  for (auto& table : this->tables) {
    if (table->created_files.size() > 0 || table->deleted_files.size() > 0) {
      std::string log_header = "";
      this->createHeaderCommit(log_header, *table);

      std::stringstream full_log;
      full_log << log_header;

      for (auto& file : table->created_files) {
        if (file.second->valid &&
            (file.first.find("lhdata") != std::string::npos ||
             file.first.find("part-") != std::string::npos)) {
          std::string entry = "";
          this->createOperationAddEntry(*(file.second), entry);

          full_log << std::endl << entry;
        }
      }

      for (auto& file : table->deleted_files) {
        if (file.second->valid &&
            (file.first.find("lhdata") != std::string::npos ||
             file.first.find("part-") != std::string::npos)) {
          std::string entry = "";
          this->createOperationDeleteEntry(*(file.second), entry);

          full_log << std::endl << entry;
        }
      }

      auto full_log_string = full_log.str();

      this->connector_ptr->write(table->marker->marker_path,
                                 full_log_string.c_str(),
                                 full_log_string.size());
      table->marker->setInvalid();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> commit_duration = end - start;
    timestamps.push_back(commit_duration.count());
  }

  return true;
}

bool LHTransactions::TransactionManagerGeneric::abort(bool clear_subdir) {
  if (this->levels[0] || this->levels[1]) {
    for (auto& table : this->tables) {
      std::string log_header = "";
      this->createHeaderCommit(log_header, *table);

      this->connector_ptr->write(table->marker->marker_path, log_header.c_str(),
                                 log_header.size());
      table->marker->setInvalid();
    }
  }

  if (clear_subdir) {
    this->clear_subdirectory();
  }

  return true;
}

bool LHTransactions::TransactionManagerGeneric::open_new_table() {
  std::string path;
  std::cin >> path;

  auto table = this->open_table(std::move(path));

  if (table) {
    this->tables.push_back(std::move(table));
    return true;
  }
  return false;
}

void LHTransactions::TransactionManagerGeneric::parse_checkpoint(
    LHTransactions::TableStats* stats, uint32_t checkpoint_nr) {
  if (!stats || checkpoint_nr == -1) {
    return;
  }

  auto checkpoint_id = std::to_string(checkpoint_nr);
  std::stringstream full_path;
  full_path << stats->base_path << "_delta_log/";

  uint32_t counter = 0;

  while (counter < (20 - checkpoint_id.size())) {
    full_path << "0";
  }

  full_path << checkpoint_id << ".checkpoint.parquet";

  std::cerr << "parsing" << full_path.str() << std::endl;

  auto content = this->connector_ptr->read2(full_path.str());

  auto list = LHHelpers::readDLCheckpoint(content->data, content->length);

  for (auto& ref : list) {
    stats->delta_log->register_operation(LHExecutor::LHDL::DLOperation::ADD,
                                         ref);
  }

  std::cerr << "done" << full_path.str() << std::endl;
}

void LHTransactions::TransactionManagerGeneric::parse_logentries2(
    uint32_t start, uint32_t end, LHTransactions::TableStats* stats,
    uint32_t max_version, bool& dependencies) {
  for (uint32_t s = start; s < end; s++) {
    auto log_version = std::to_string(s);
    std::stringstream path;
    path << stats->base_path << "_delta_log/";

    for (uint32_t i = 0; i < 20 - log_version.size(); i++) {
      path << "0";
    }

    path << log_version << ".json";

    if (s <= max_version) {
      std::string in = "";

      while (true) {
        auto content = connector_ptr->read2(path.str());
        if (content->data) {
          in = std::string(content->data, content->length);
          break;
        }
      }

      while (in.size() > 0) {
        auto pos = in.find_first_of('\n');

        std::string single_entry;
        if (pos != std::string_view::npos) {
          if (in.size() == 0) {
            std::cerr << "4" << std::endl;
          }
          single_entry = in.substr(0, pos);
        } else {
          single_entry = in;
          pos = in.size();
        }

        if (single_entry.size() > 1) {
          auto parsed = LHHelpers::readJSON_rapid(single_entry.c_str());
          auto dep = this->parseEntry(*parsed, stats->delta_log.get());
          if (dep != UINT32_MAX) {
            if (this->levels[0] || this->levels[1]) {
              this->prev_markers.push_back(path.str());
            }

            if (this->levels[1]) {
              if (this->relation->register_dependency(dep)) {
                dependencies = true;
              }
            }
            break;
          }
        }

        if (pos + 1 < +in.size()) {
          if (in.size() == 0) {
            std::cerr << "5" << std::endl;
          }
          in = in.substr(pos + 1, in.size());
        } else {
          in = "";
        }
      }
    } else if (this->levels[0] || this->levels[1]) {
      this->prev_markers.push_back(path.str());
    }
  }
}

void LHTransactions::TransactionManagerGeneric::parse_logentries(
    uint32_t start, uint32_t end, Aws::Vector<Aws::S3::Model::Object>& list,
    LHTransactions::TableStats* stats, uint32_t max_version,
    bool& dependencies) {
  for (uint32_t s = start; s < end; s++) {
    const auto& line = list[s].GetKey();

    if (line.find(".json") != std::string::npos) {
      auto pos_start = line.find("/_delta_log/") + 12;
      auto pos_end = line.find(".json") - 5;

      if (line.size() == 0) {
        std::cerr << "3" << std::endl;
      }
      auto log_number = std::stoi(line.substr(pos_start, pos_end));

      if (log_number <= max_version) {
        std::string in = "";

        while (true) {
          auto content = connector_ptr->read2(line);
          if (content->data) {
            in = std::string(content->data, content->length);
            break;
          }
        }

        while (in.size() > 0) {
          auto pos = in.find_first_of('\n');

          std::string single_entry;
          if (pos != std::string_view::npos) {
            if (in.size() == 0) {
              std::cerr << "4" << std::endl;
            }
            single_entry = in.substr(0, pos);
          } else {
            single_entry = in;
            pos = in.size();
          }
          if (single_entry.size() > 1) {
            auto parsed = LHHelpers::readJSON_rapid(single_entry.c_str());
            auto dep = this->parseEntry(*parsed, stats->delta_log.get());
            if (dep != UINT32_MAX) {
              if (this->levels[0] || this->levels[1]) {
                this->prev_markers.push_back(line);
              }

              if (this->levels[1]) {
                if (this->relation->register_dependency(dep)) {
                  dependencies = true;
                }
              }
              break;
            }
          }

          if (pos + 1 < +in.size()) {
            if (in.size() == 0) {
              std::cerr << "5" << std::endl;
            }
            in = in.substr(pos + 1, in.size());
          } else {
            in = "";
          }
        }
      } else if (this->levels[0] || this->levels[1]) {
        this->prev_markers.push_back(line);
      }
    }
  }
}

std::unique_ptr<LHTransactions::TableStats>
LHTransactions::TransactionManagerGeneric::open_tableV2(
    std::string&& table_path, uint32_t num_threads) {
  uint32_t max_version = UINT32_MAX;
  if (this->levels[2]) {
    if (this->local_log.count(table_path) != 0) {
      max_version = this->local_log.find(table_path)->second;
    }
  }

  auto table =
      std::make_unique<LHTransactions::TableStats>(std::move(table_path));
  table->table_id = this->tables.size();

  std::stringstream last_checkpoint;
  last_checkpoint << table->base_path << "_delta_log/_last_checkpoint";

  uint32_t checkpoint_version = -1;

  if (this->connector_ptr->check(last_checkpoint.str())) {
    std::string content;
    while (true) {
      auto result = this->connector_ptr->read2(last_checkpoint.str());
      if (result->data != nullptr) {
        content = std::string(result->data, result->length);
        break;
      }
    }
    checkpoint_version = std::stoi(content);
  }

  uint32_t highest_version = -1;

  if (levels[2]) {
    if (this->local_log.count(table->base_path) != 0) {
      highest_version = this->local_log.find(table->base_path)->second;
    }
  }

  if (highest_version == -1) {
    std::stringstream delta_log;
    delta_log << table->base_path << "_delta_log";

    Aws::Vector<Aws::S3::Model::Object> file_list;
    while (true) {
      auto outcome = this->connector_ptr->list2_vec(delta_log.str());
      if (outcome.IsSuccess()) {
        file_list = outcome.GetResult().GetContents();
        break;
      }
    }

    highest_version = max_version;
    for (auto& ref : file_list) {
      if (ref.GetKey().find(".json") != std::string::npos) {
        auto pos_start = ref.GetKey().find("/_delta_log/") + 12;
        auto pos_end = ref.GetKey().find(".json") - 5;

        if (ref.GetKey().size() == 0) {
          std::cerr << "6" << std::endl;
        }
        auto log_number = std::stoi(ref.GetKey().substr(pos_start, pos_end));
        if (log_number > highest_version || highest_version == UINT32_MAX) {
          highest_version = log_number;
        }
      }
    }
  }

  while (true) {
    auto marker_version = std::to_string(highest_version + 1);
    std::stringstream path;
    path << table->base_path << "_delta_log/";

    for (uint32_t i = 0; i < 20 - marker_version.size(); i++) {
      path << "0";
    }

    path << marker_version << ".json";

    bool got_marker = true;

    if (this->levels[0] || this->levels[1]) {
      if (this->connector_ptr->check(path.str())) {
        highest_version++;
      } else {
        std::string marker_header = "";
        std::string marker_entry = "";

        this->createHeaderMarker(marker_header);
        this->createMarkerEntry(this->working_dir, marker_entry);

        std::string complete = marker_header + "\n" + marker_entry;
        got_marker = connector_ptr->write_atomic(path.str(), complete.c_str(),
                                                 complete.size());

        if (got_marker) {
          table->marker = std::make_unique<LHTransactions::Marker>(
              path.str(), this->connector_ptr);
          table->marker_version = highest_version + 1;
          break;
        } else {
          highest_version++;
        }
      }
    } else {
      table->marker = std::make_unique<LHTransactions::Marker>(
          path.str(), this->connector_ptr);
      table->marker->setInvalid();
      table->marker_version = highest_version + 1;
      table->delta_log->register_read_version(highest_version);
      table->delta_log->register_write_version(highest_version + 1);
      break;
    }
  }

  std::thread sub_thread([this, &table, &checkpoint_version] {
    this->parse_checkpoint(table.get(), checkpoint_version);
  });

  uint32_t val_log_entries = checkpoint_version == -1
                                 ? highest_version + 1
                                 : highest_version - checkpoint_version + 1;

  uint32_t required_threads =
      val_log_entries < num_threads ? val_log_entries : num_threads;
  uint32_t step_size = val_log_entries / required_threads;
  uint32_t special_steps = val_log_entries % required_threads;

  std::vector<std::thread> threads;

  uint32_t start = checkpoint_version == -1 ? 0 : checkpoint_version;

  bool dependencies = false;

  for (uint32_t i = 0; i < required_threads; i++) {
    uint32_t end = start + step_size;
    if (special_steps > 0) {
      end++;
      special_steps--;
    }
    threads.emplace_back(
        [this, start, end, &table, &max_version, &dependencies] {
          this->parse_logentries2(start, end, table.get(), max_version,
                                  dependencies);
        });

    start = end;
  }

  table->read_version = highest_version;

  if (this->levels[2] && max_version == UINT32_MAX) {
    this->local_log.insert({table->base_path, highest_version});
  }

  sub_thread.join();
  for (auto& t : threads) {
    t.join();
  }

  table->in_bytes = 0;
  table->out_bytes = 0;
  table->file_num = 0;

  if (dependencies) {
    bool fixed = false;
    while (!fixed) {
      this->abort(false);
      fixed = this->reopen_tables();
    }
  }
  return std::move(table);
}

bool LHTransactions::TransactionManagerGeneric::reopen_tables(
    uint32_t num_threads) {
  bool dependencies = false;
  this->relation = std::make_unique<LHTransactions::GlobalRelation>();
  if (this->levels[2]) {
    this->local_log.clear();
    this->global_log_exists = this->parse_global_snapshots();
  }

  for (auto& table : this->tables) {
    uint32_t max_version = UINT32_MAX;
    if (this->levels[2]) {
      if (this->local_log.count(table->base_path) != 0) {
        max_version = this->local_log.find(table->base_path)->second;
      }
    }

    std::string copy_path = table->base_path;

    table->delta_log = std::make_unique<LHExecutor::LHDL::DeltaLogManager>(
        std::move(copy_path));
    table->file_num = 0;
    table->in_bytes = 0;
    table->out_bytes = 0;

    table->prev_marker = false;
    table->opened = true;

    std::stringstream last_checkpoint;
    last_checkpoint << table->base_path << "_delta_log/_last_checkpoint";

    uint32_t checkpoint_version = -1;

    if (this->connector_ptr->check(last_checkpoint.str())) {
      std::string content;
      while (true) {
        auto result = this->connector_ptr->read2(last_checkpoint.str());
        if (result->data != nullptr) {
          content = std::string(result->data, result->length);
          break;
        }
      }
      checkpoint_version = std::stoi(content);
    }

    uint32_t highest_version = -1;

    if (levels[2]) {
      if (this->local_log.count(table->base_path) != 0) {
        highest_version = this->local_log.find(table->base_path)->second;
      }
    }

    if (highest_version == -1) {
      std::stringstream delta_log;
      delta_log << table->base_path << "_delta_log";

      Aws::Vector<Aws::S3::Model::Object> file_list;

      while (true) {
        auto outcome = this->connector_ptr->list2_vec(delta_log.str());
        if (outcome.IsSuccess()) {
          file_list = outcome.GetResult().GetContents();
          break;
        }
      }

      highest_version = max_version;
      for (auto& ref : file_list) {
        if (ref.GetKey().find(".json") != std::string::npos) {
          auto pos_start = ref.GetKey().find("/_delta_log/") + 12;
          auto pos_end = ref.GetKey().find(".json") - 5;

          if (ref.GetKey().size() == 0) {
            std::cerr << "6" << std::endl;
          }
          auto log_number = std::stoi(ref.GetKey().substr(pos_start, pos_end));
          if (log_number > highest_version || highest_version == UINT32_MAX) {
            highest_version = log_number;
          }
        }
      }
    }

    while (true) {
      auto marker_version = std::to_string(highest_version + 1);
      std::stringstream path;
      path << table->base_path << "_delta_log/";

      for (uint32_t i = 0; i < 20 - marker_version.size(); i++) {
        path << "0";
      }

      path << marker_version << ".json";

      bool got_marker = true;

      if (this->levels[0] || this->levels[1]) {
        if (this->connector_ptr->check(path.str())) {
          highest_version++;
        } else {
          std::string marker_header = "";
          std::string marker_entry = "";

          this->createHeaderMarker(marker_header);
          this->createMarkerEntry(this->working_dir, marker_entry);

          std::string complete = marker_header + "\n" + marker_entry;
          got_marker = connector_ptr->write_atomic(path.str(), complete.c_str(),
                                                   complete.size());

          if (got_marker) {
            table->marker = std::make_unique<LHTransactions::Marker>(
                path.str(), this->connector_ptr);
            table->marker_version = highest_version + 1;
            break;
          } else {
            highest_version++;
          }
        }
      }
    }

    std::thread sub_thread([this, &table, &checkpoint_version] {
      this->parse_checkpoint(table.get(), checkpoint_version);
    });

    uint32_t val_log_entries = checkpoint_version == -1
                                   ? highest_version + 1
                                   : highest_version - checkpoint_version + 1;

    uint32_t required_threads =
        val_log_entries < num_threads ? val_log_entries : num_threads;
    uint32_t step_size = val_log_entries / required_threads;
    uint32_t special_steps = val_log_entries % required_threads;

    std::vector<std::thread> threads;

    uint32_t start = checkpoint_version == -1 ? 0 : checkpoint_version;

    bool dependencies = false;

    for (uint32_t i = 0; i < required_threads; i++) {
      uint32_t end = start + step_size;
      if (special_steps > 0) {
        end++;
        special_steps--;
      }
      threads.emplace_back(
          [this, start, end, &table, &max_version, &dependencies] {
            this->parse_logentries2(start, end, table.get(), max_version,
                                    dependencies);
          });

      start = end;
    }

    table->read_version = highest_version;

    if (this->levels[2] && max_version == UINT32_MAX) {
      this->local_log.insert({table->base_path, highest_version});
    }

    sub_thread.join();
    for (auto& t : threads) {
      t.join();
    }

    if (dependencies) {
      bool fixed = false;
      while (!fixed) {
        fixed = this->reopen_tables(num_threads);
      }
    }
  }

  return true;
}

std::unique_ptr<LHTransactions::TableStats>
LHTransactions::TransactionManagerGeneric::open_table(
    std::string&& table_path) {
  return nullptr;
}

// version 21.7.24
bool LHTransactions::TransactionManagerGeneric::begin(std::string base_path) {
  this->tables.clear();
  this->created_files.clear();
  this->deleted_files.clear();
  this->found = {};
  std::stringstream work_place;

  work_place << base_path << "_delta_log/" << std::rand() << "-"
             << this->transaction_id << "/";

  this->working_dir = work_place.str();

  std::unique_ptr<LHTransactions::TableStats> table = nullptr;

  std::thread sub_thread([this] {
    if (this->levels[0]) {
      bool outcome = false;
      std::string init_log = "init transaction";
      while (!outcome) {
        outcome = this->connector_ptr->write(this->working_dir + "000.sub",
                                             init_log.c_str(), init_log.size());
      }
    }
  });

  if (this->levels[2]) {
    this->global_log_exists = this->parse_global_snapshots();
  }

  while (!table) {
    table = this->open_tableV2(std::move(base_path));
  }

  this->tables.push_back(std::move(table));

  sub_thread.join();

  this->next_step = 1;

  return true;
}

bool LHTransactions::TransactionManagerGeneric::reroll() {
  uint32_t current_reroll_step = this->next_step - 1;

  std::string redo = "";
  std::string undo = "";

  while (true) {
    if (current_reroll_step <= 0) {
      current_reroll_step = 1;
    }

    std::stringstream sublog_key;
    sublog_key << this->working_dir << "00" << current_reroll_step << ".sub";

    auto result = this->connector_ptr->read2(sublog_key.str());

    auto content = std::string(result->data, result->length);

    if (content.size() == 0) {
      std::cerr << "9" << std::endl;
    }
    redo = content.substr(0, content.find('\n'));
    undo = content.substr(content.find('\n') + 1);

    std::cout << "We are at step " << current_reroll_step << std::endl;
    std::cout << "Redo: " << redo << std::endl;
    std::cout << "Undo: " << undo << std::endl;
    std::cout << "What do you want to do?" << std::endl
              << "[0] Redo" << std::endl
              << "[1] Undo" << std::endl
              << "[2] complete reroll" << std::endl;

    std::string in;
    std::cin >> in;

    std::string execution_string = "";

    switch (std::stoi(in)) {
      case 0: {
        execution_string = redo;
        current_reroll_step++;
      } break;
      case 1: {
        execution_string = undo;
        current_reroll_step--;
      } break;
      default: {
        return true;
      }
    }

    if (!this->execute_reroll_step(execution_string)) {
      return false;
    }
  }

  return true;
}

bool LHTransactions::TransactionManagerGeneric::undo(int untilStep) {
  uint32_t current_undo_step = this->next_step - 1;

  std::string redo = "";
  std::string undo = "";

  while (current_undo_step > untilStep) {
    std::stringstream sublog_key;
    sublog_key << this->working_dir << "00" << current_undo_step << ".sub";
    auto result = this->connector->read2(sublog_key.str());

    if (result->data) {
      auto content = std::string(result->data, result->length);

      undo = content.substr(content.find('\n') + 1);

      std::string execution_string = "";

      if (this->execute_reroll_step(undo)) {
        current_undo_step--;
      } else if (!undo.empty()) {
        current_undo_step++;
      } else {
        return false;
      }
    } else {
      current_undo_step--;
    }
  }

  return true;
}

bool LHTransactions::TransactionManagerGeneric::redo(int atStep) {
  uint32_t current_redo_step = atStep;
  this->times_redo_num++;
  std::string redo = "";
  std::string undo = "";

  while (current_redo_step < this->next_step) {
    std::stringstream sublog_key;
    sublog_key << this->working_dir << "00" << current_redo_step << ".sub";
    auto result = this->connector_ptr->read2(sublog_key.str());

    if (result->data) {
      auto content = std::string(result->data, result->length);
      if (content.size() == 0) {
        std::cerr << "10" << std::endl;
      }
      redo = content.substr(0, content.find('\n'));

      std::string execution_string = "";

      if (this->execute_reroll_step(redo)) {
        current_redo_step++;
      } else if (!undo.empty()) {
        current_redo_step--;
      } else {
        return false;
      }
    } else {
      current_redo_step++;
    }
  }

  return true;
}

std::pair<std::string, LHExecutor::LHDL::DLOperation>
LHTransactions::TransactionManagerGeneric::parseEntry(
    rapidjson::Document& val) {
  LHExecutor::LHDL::DLOperation op = LHExecutor::LHDL::DLOperation::NONE;
  std::string content = "";

  bool is_entry_or_invalid = true;

  if (val.HasMember("metaData")) {
    op = LHExecutor::LHDL::DLOperation::METADATA;
  }

  if (val.HasMember("add")) {
    op = LHExecutor::LHDL::DLOperation::ADD;
    if (val["add"].HasMember("path")) {
      content = val["add"]["path"].GetString();
    }
  }

  if (val.HasMember("remove")) {
    op = LHExecutor::LHDL::DLOperation::DELETE;
    content = val["remove"]["path"].GetString();
  }

  if (val.HasMember("marker")) {
    if (val["marker"].HasMember("running")) {
      this->explicit_log = false;
      op = LHExecutor::LHDL::DLOperation::MARKER;
    }
  }

  return {content, op};
}

// returns is entry is an active marker
uint32_t LHTransactions::TransactionManagerGeneric::parseEntry(
    rapidjson::Document& val, LHExecutor::LHDL::DeltaLogManager* manager) {
  LHExecutor::LHDL::DLOperation op = LHExecutor::LHDL::DLOperation::NONE;
  std::string content = "";

  bool is_entry_or_invalid = true;

  if (val.HasMember("metaData")) {
    op = LHExecutor::LHDL::DLOperation::METADATA;
    // TODO: add metadata here
    // content = val["metaData"];
    manager->register_operation(op, content);
  }

  if (val.HasMember("add")) {
    op = LHExecutor::LHDL::DLOperation::ADD;
    if (val["add"].HasMember("path")) {
      content = val["add"]["path"].GetString();
    }
    manager->register_operation(op, content, val);
  }

  if (val.HasMember("remove")) {
    op = LHExecutor::LHDL::DLOperation::DELETE;
    content = val["remove"]["path"].GetString();
    manager->register_operation(op, content);
  }

  if (val.HasMember("marker")) {
    if (val["marker"].HasMember("running")) {
      // a concurrent transaction placed a marker
      this->explicit_log = false;

      if (val["marker"].HasMember("txnid") && val["marker"]["txnid"].IsUint()) {
        return val["marker"]["txnid"].GetUint();
      }
    }
  }

  return UINT32_MAX;
}

void LHTransactions::TransactionManagerGeneric::createHeaderMarker(
    std::string& out_string) {
  rapidjson::Document d;
  d.SetObject();

  rapidjson::Value operation_para;
  rapidjson::Value operation_metrics;

  operation_para.SetObject();
  operation_metrics.SetObject();

  operation_para.AddMember("mode", "Marker", d.GetAllocator());
  operation_para.AddMember("partitionBy", "[]", d.GetAllocator());

  operation_metrics.AddMember("numOutputBytes", "0", d.GetAllocator());
  operation_metrics.AddMember("numOutputRows", "0", d.GetAllocator());
  operation_metrics.AddMember("numFiles", "0", d.GetAllocator());

  rapidjson::Value commit_info;
  commit_info.SetObject();

  commit_info.AddMember("txnId", this->transaction_id, d.GetAllocator());
  commit_info.AddMember("engineInfo", "LHTransactions 0.1 - Delta Lake 3.0",
                        d.GetAllocator());
  commit_info.AddMember("operationMetrics", operation_metrics,
                        d.GetAllocator());
  commit_info.AddMember("isBlindAppend", true, d.GetAllocator());
  commit_info.AddMember("isolationLevel", "Serializable", d.GetAllocator());
  commit_info.AddMember("readVersion", 0, d.GetAllocator());
  commit_info.AddMember("operationParameters", operation_para,
                        d.GetAllocator());
  commit_info.AddMember("operation", "WRITE", d.GetAllocator());
  commit_info.AddMember("timestamp", 0, d.GetAllocator());

  d.AddMember("commitInfo", commit_info, d.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  out_string = buffer.GetString();
}

bool LHTransactions::TransactionManagerGeneric::checkMarker(
    const rapidjson::Document* content) {
  if (content && content->HasMember("commitInfo")) {
    auto& tmp = content->FindMember("commitInfo")->value;
    if (tmp.HasMember("txnId") &&
        tmp.FindMember("txnId")->value.GetInt() == this->transaction_id) {
      return true;
    }
  }

  return false;
}

// simplified version for the test
void LHTransactions::TransactionManagerGeneric::createMarkerEntry(
    std::string& working_dir, std::string& out_str) {
  rapidjson::Document d;
  d.SetObject();
  rapidjson::Value marker;
  marker.SetObject();

  rapidjson::Value val;
  val.SetString(working_dir.c_str(), working_dir.size());
  marker.AddMember("workDir", val, d.GetAllocator());
  marker.AddMember("running", true, d.GetAllocator());

  auto dep = this->relation->get_dependencies();
  rapidjson::Value arr;
  arr.SetArray();
  for (auto& elem : dep) {
    rapidjson::Value e;
    e.SetUint(elem);
    arr.PushBack(e, d.GetAllocator());
  }
  marker.AddMember("dependencies", arr, d.GetAllocator());
  marker.AddMember("txnid", this->transaction_id, d.GetAllocator());

  d.AddMember("marker", marker, d.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  out_str = buffer.GetString();
}

void LHTransactions::TransactionManagerGeneric::createHeaderCommit(
    std::string& out_string, LHTransactions::TableStats& stats) {
  rapidjson::Document d;
  d.SetObject();

  rapidjson::Value operation_para;
  rapidjson::Value operation_metrics;

  operation_para.SetObject();
  operation_metrics.SetObject();

  operation_para.AddMember("mode", "MultiQuery", d.GetAllocator());
  operation_para.AddMember("partitionBy", "[]", d.GetAllocator());

  operation_metrics.AddMember("numOutputBytes", stats.out_bytes,
                              d.GetAllocator());
  operation_metrics.AddMember("numOutputRows", stats.in_bytes,
                              d.GetAllocator());
  operation_metrics.AddMember("numFiles", stats.file_num, d.GetAllocator());

  rapidjson::Value commit_info;
  commit_info.SetObject();

  commit_info.AddMember("txnId", "1234", d.GetAllocator());
  commit_info.AddMember("engineInfo", "LHTransactions 0.1 - Delta Lake 3.0",
                        d.GetAllocator());
  commit_info.AddMember("operationMetrics", operation_metrics,
                        d.GetAllocator());
  commit_info.AddMember("isBlindAppend", false, d.GetAllocator());
  commit_info.AddMember("isolationLevel", "Serializable", d.GetAllocator());
  commit_info.AddMember("readVersion", stats.read_version, d.GetAllocator());
  commit_info.AddMember("operationParameters", operation_para,
                        d.GetAllocator());
  commit_info.AddMember("operation", "WRITE", d.GetAllocator());
  commit_info.AddMember("timestamp", 0, d.GetAllocator());

  d.AddMember("commitInfo", commit_info, d.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  out_string = buffer.GetString();
}

// simplified version
void LHTransactions::TransactionManagerGeneric::createOperationAddEntry(
    FileStats& file, std::string& out_str) {
  rapidjson::Document d;
  d.SetObject();
  rapidjson::Document d2;
  d2.SetObject();
  rapidjson::Value minValues;
  rapidjson::Value maxValues;
  rapidjson::Value nullCount;

  minValues.SetObject();
  maxValues.SetObject();
  nullCount.SetObject();

  for (size_t i = 0; i < file.name.size(); i++) {
    rapidjson::Value test, test2;
    rapidjson::Value name, name2, name3;
    name.SetString(
        rapidjson::StringRef(file.name[i].c_str(), file.name[i].size()));
    name2.SetString(
        rapidjson::StringRef(file.name[i].c_str(), file.name[i].size()));
    name3.SetString(
        rapidjson::StringRef(file.name[i].c_str(), file.name[i].size()));
    test.SetString(file.min_val[i].c_str(), file.min_val[i].size());
    test2.SetString(file.max_val[i].c_str(), file.max_val[i].size());
    minValues.AddMember(name, test, d2.GetAllocator());
    maxValues.AddMember(name2, test2, d2.GetAllocator());
    nullCount.AddMember(name3, 0, d2.GetAllocator());
  }

  d2.AddMember("numRecords", 1, d2.GetAllocator());
  d2.AddMember("minValues", minValues, d2.GetAllocator());
  d2.AddMember("maxValues", maxValues, d2.GetAllocator());

  rapidjson::StringBuffer buffer1;
  rapidjson::Writer<rapidjson::StringBuffer> writer1(buffer1);
  d2.Accept(writer1);

  rapidjson::Value add;
  add.SetObject();
  if (file.path.size() == 0) {
    std::cerr << "11" << std::endl;
  }
  auto sub_pos = file.path.find("lhdata-");
  if (sub_pos == std::string::npos) {
    sub_pos = file.path.find("part-");
  }

  if (sub_pos == std::string::npos) {
    std::cerr << "unknown file naming" << std::endl;
  }
  auto path_substr = file.path.substr(sub_pos);
  rapidjson::Value path;
  path.SetString(path_substr.c_str(), path_substr.size());
  add.AddMember("path", path, d.GetAllocator());
  add.AddMember("partitionValues", {}, d.GetAllocator());
  add.AddMember("size", file.size, d.GetAllocator());
  add.AddMember("modificationTime", 0, d.GetAllocator());
  add.AddMember("dataChange", true, d.GetAllocator());
  std::string sub_str = buffer1.GetString();
  rapidjson::Value stats_str;
  stats_str.SetString(sub_str.c_str(), sub_str.size());
  add.AddMember("stats", stats_str, d.GetAllocator());

  d.AddMember("add", add, d.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  out_str = buffer.GetString();
}

// simplified version
void LHTransactions::TransactionManagerGeneric::createOperationDeleteEntry(
    FileStats& file, std::string& out_str) {
  rapidjson::Document d;
  d.SetObject();
  rapidjson::Value remove;
  remove.SetObject();
  if (file.path.size() == 0) {
    std::cerr << "12" << std::endl;
  }
  auto path_str = file.path.substr(this->base_path.size());
  rapidjson::Value path;
  path.SetString(path_str.c_str(), path_str.size());
  remove.AddMember("path", path, d.GetAllocator());
  remove.AddMember("deletionTimestamp", 0, d.GetAllocator());
  remove.AddMember("dataChange", true, d.GetAllocator());
  remove.AddMember("extendedFileMetadata", true, d.GetAllocator());
  remove.AddMember("partitionValues", {}, d.GetAllocator());
  remove.AddMember("size", file.size, d.GetAllocator());

  d.AddMember("remove", remove, d.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  out_str = buffer.GetString();
}

bool LHTransactions::TransactionManagerGeneric::add_substep(
    std::string& file, LHTransactions::TransactionManager::SubOpType type,
    LHTransactions::TableStats& table, const std::string* key) {
  ops.push_back({type, file});

  if (this->levels[0]) {
    if (this->explicit_log) {
      return add_substep_explicit(file, type, table, key);
    } else {
      return add_substep_implicit(file, type, table, key);
    }
  }
  return true;
}

bool LHTransactions::TransactionManagerGeneric::add_substep_explicit(
    std::string& file, LHTransactions::TransactionManager::SubOpType type,
    LHTransactions::TableStats& table, const std::string* key) {
  std::stringstream sublog_key;
  std::stringstream sublog_content;
  sublog_key << this->working_dir << "00" << this->next_step << ".sub";

  switch (type) {
    case LHTransactions::TransactionManager::SubOpType::READ:
      sublog_content << table.table_id << " r " << file << "\n";
      sublog_content << table.table_id << " r " << file;
      break;
    case LHTransactions::TransactionManager::SubOpType::ADD:
      sublog_content << table.table_id << " a " << file << "\n"
                     << table.table_id << " d " << file;
      break;
    case LHTransactions::TransactionManager::SubOpType::DELETE:
      sublog_content << table.table_id << " d " << file << "\n"
                     << table.table_id << " a " << file;
      break;
    default:
      break;
  }

  std::string sublog_c = sublog_content.str();
  connector_ptr->write(sublog_key.str(), sublog_c.c_str(), sublog_c.size());
  this->next_step++;

  return true;
}

bool LHTransactions::TransactionManagerGeneric::add_substep_implicit(
    std::string& file, LHTransactions::TransactionManager::SubOpType type,
    LHTransactions::TableStats& table, const std::string* key) {
  std::stringstream sublog_key;
  std::stringstream sublog_content;
  sublog_key << this->working_dir << "00" << this->next_step << ".sub";

  switch (type) {
    case LHTransactions::TransactionManager::SubOpType::READ:

      if (key) {
        sublog_content << table.table_id << " s " << *key << "\n";
        sublog_content << table.table_id << " o " << this->found.second;
      } else {
        sublog_content << table.table_id << " r " << file << "\n";
        sublog_content << table.table_id << " r " << file;
      }
      break;
    case LHTransactions::TransactionManager::SubOpType::ADD:
      if (key) {
        sublog_content << table.table_id << " e found "
                       << this->update_vals.size() - 1 << "\n"
                       << table.table_id << " d found";
        this->found = {*key, file};
      } else {
        sublog_content << table.table_id << " a " << file << "\n"
                       << table.table_id << " d " << file;
      }
      // table.read_version++;
      break;
    case LHTransactions::TransactionManager::SubOpType::DELETE:
      if (key) {
        sublog_content << table.table_id << " d found" << *key << "\n"
                       << table.table_id << " a found "
                       << this->update_ops.size() - 1;
        this->found = {*key, file};
      } else {
        sublog_content << table.table_id << " d " << "\n"
                       << table.table_id << " a " << file;
      }
      break;
    default:
      break;
  }

  std::string sublog_c = sublog_content.str();
  connector_ptr->write(sublog_key.str(), sublog_c.c_str(), sublog_c.size());
  this->next_step++;

  return true;
}

void LHTransactions::TransactionManagerGeneric::clear_subdirectory() {
  if (this->levels[0]) {
    for (int log_id = 0; log_id < this->next_step; log_id++) {
      std::stringstream sublog_key;
      sublog_key << this->working_dir << "00" << log_id << ".sub";

      this->connector_ptr->delete_file(sublog_key.str());
    }
  }
}

bool LHTransactions::TransactionManagerGeneric::execute_reroll_step(
    std::string exec_string) {
  uint32_t pos = exec_string.find(' ');
  if (exec_string.size() == 0) {
    std::cerr << "13" << std::endl;
  }
  std::string table_id = exec_string.substr(0, pos);

  auto& table = this->tables[std::stoi(table_id)];

  std::string command = exec_string.substr(pos + 1);

  if (command.size() == 0) {
    std::cerr << "14" << std::endl;
  }

  std::cerr << command << std::endl;
  std::string argument = command.substr(2);

  if (command[0] == 'r') {
    auto result = this->connector_ptr->read2(argument);

    auto decoded = LHHelpers::readParquet(result->data, result->length);

    return true;
  }

  if (command[0] == 's') {
    this->found = {argument, table->delta_log->findFileWithStats(0, argument)};

    return true;
  }

  if (command[0] == 'o') {
    this->found = {"", argument};

    return true;
  }

  if (command[0] == 'a') {
    this->times_redo++;
    if (argument.find("found") != std::string::npos) {
      if (argument.size() == 0) {
        std::cerr << "15" << std::endl;
      }
      auto sub = argument.substr(6);
      auto& values = update_vals[std::stoi(argument.substr(6))];
      auto result = this->connector_ptr->read2(this->found.second);

      auto arrowBaseTable =
          LHHelpers::readParquetAsTable(result->data, result->length);
      for (auto& ref : values) {
        auto col_names = arrowBaseTable->ColumnNames();
        for (int i = 0; i < col_names.size(); i++) {
          if (col_names[i].compare(ref.first) == 0) {
            auto field =
                std::make_shared<arrow::Field>(ref.first, arrow::utf8());

            arrow::StringBuilder builder;
            PARQUET_THROW_NOT_OK(builder.Append(ref.second));
            std::shared_ptr<arrow::Array> arrow_array;
            std::vector<std::shared_ptr<arrow::Array>> chunks;

            PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));
            chunks.push_back(std::move(arrow_array));

            auto new_table = arrowBaseTable->SetColumn(
                i, field,
                std::make_shared<arrow::ChunkedArray>(std::move(chunks)));

            arrowBaseTable = *new_table;
          }
        }
      }

      auto encoded = LHHelpers::encodeAsParquet(arrowBaseTable);
      std::string path;

      while (true) {
        std::stringstream stream;

        stream << table->base_path << "lhdata-" << this->transaction_id << "-"
               << std::time(0) << ".parquet";

        if (!this->connector_ptr->check(stream.str())) {
          path = stream.str();
          break;
        }
      }

      if (!this->connector_ptr->write(path, encoded->first, encoded->second)) {
        return false;
      }

      auto stats = std::make_unique<FileStats>();
      stats->min_val = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
      stats->min_val = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
      stats->name = {"YCSB_KEY", "field0", "field1", "field2",
                     "field3",   "field4", "field5", "field6",
                     "field7",   "field8", "field9"};
      stats->path = path;
      stats->size = encoded->second;
      stats->valid = true;

      table->created_files.insert({path, std::move(stats)});

      table->created_files.find(path)->second->min_val = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
      table->created_files.find(path)->second->max_val = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};

      if (table->deleted_files.count(path) != 0) {
        table->deleted_files.find(path)->second->valid = false;
      }

      std::vector<std::string> stat_keys = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
      table->delta_log->register_operation(LHExecutor::LHDL::DLOperation::ADD,
                                           path, stat_keys, stat_keys, true);

    } else {
      table->delta_log->register_operation(LHExecutor::LHDL::DLOperation::ADD,
                                           argument, true, true);

      if (table->created_files.count(argument) != 0) {
        // REDO step
        table->created_files.find(argument)->second->valid = true;
      }

      if (table->deleted_files.count(argument) != 0) {
        // UNDO step
        table->deleted_files.find(argument)->second->valid = false;
      }
    }

    return true;
  }

  if (command[0] == 'e') {
    this->times_redo++;
    if (argument.find("found") != std::string::npos) {
      if (argument.size() == 0) {
        std::cerr << "15" << std::endl;
      }
      auto tmp1 = update_vals.size();
      auto tmp2 = update_ops.size();
      auto sub_key = std::stoi(argument.substr(6, 7));
      auto& values = update_vals[sub_key];
      auto& op = update_ops[sub_key];
      auto result = this->connector_ptr->read2(this->found.second);

      auto arrowBaseTable =
          LHHelpers::readParquetAsTable(result->data, result->length);

      for (auto& ref : values) {
        std::shared_ptr<arrow::Array> arrow_array;

        auto col = arrowBaseTable->GetColumnByName(ref.first);
        if (arrowBaseTable->schema()->GetFieldByName(ref.first)->type()->Equals(
                arrow::utf8())) {
          arrow_array = this->build_string_col(ref.first, col, op, ref.second);
          auto field = std::make_shared<arrow::Field>(ref.first, arrow::utf8());
          std::vector<std::shared_ptr<arrow::Array>> chunks;

          chunks.push_back(std::move(arrow_array));

          auto new_table = arrowBaseTable->SetColumn(
              arrowBaseTable->schema()->GetFieldIndex(ref.first), field,
              std::make_shared<arrow::ChunkedArray>(std::move(chunks)));

          arrowBaseTable = *new_table;
        } else if (arrowBaseTable->schema()
                       ->GetFieldByName(ref.first)
                       ->type()
                       ->Equals(arrow::int32())) {
          arrow_array =
              this->build_int_col(ref.first, col, op, std::stoi(ref.second));
          auto field =
              std::make_shared<arrow::Field>(ref.first, arrow::int32());
          std::vector<std::shared_ptr<arrow::Array>> chunks;

          chunks.push_back(std::move(arrow_array));

          auto new_table = arrowBaseTable->SetColumn(
              arrowBaseTable->schema()->GetFieldIndex(ref.first), field,
              std::make_shared<arrow::ChunkedArray>(std::move(chunks)));
          arrowBaseTable = *new_table;
        } else {
          std::cerr << "data type "
                    << arrowBaseTable->schema()
                           ->GetFieldByName(ref.first)
                           ->type()
                           ->ToString()
                    << "not implemented; skipping instruction" << std::endl;
        }
      }

      auto encoded = LHHelpers::encodeAsParquet(arrowBaseTable);
      std::string path;

      while (true) {
        std::stringstream stream;

        stream << table->base_path << "lhdata-" << this->transaction_id << "-"
               << std::time(0) << ".parquet";

        if (!this->connector_ptr->check(stream.str())) {
          path = stream.str();
          break;
        }
      }

      if (!this->connector_ptr->write(path, encoded->first, encoded->second)) {
        return false;
      }

      auto stats = std::make_unique<FileStats>();
      stats->min_val = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
      stats->min_val = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
      stats->name = {"YCSB_KEY", "field0", "field1", "field2",
                     "field3",   "field4", "field5", "field6",
                     "field7",   "field8", "field9"};
      stats->path = path;
      stats->size = encoded->second;
      stats->valid = true;

      table->created_files.insert({path, std::move(stats)});

      table->created_files.find(path)->second->min_val = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
      table->created_files.find(path)->second->max_val = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};

      if (table->created_files.count(path) != 0) {
        // REDO step
        table->created_files.find(path)->second->valid = true;
      }

      if (table->deleted_files.count(path) != 0) {
        // UNDO step
        table->deleted_files.find(path)->second->valid = false;
      }

      std::vector<std::string> stat_keys = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};

      table->delta_log->register_operation(LHExecutor::LHDL::DLOperation::ADD,
                                           path, stat_keys, stat_keys, true);

      auto stats2 = std::make_unique<FileStats>();
      // FileStats stats;
      stats2->min_val = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
      stats2->min_val = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
      stats2->name = {"YCSB_KEY", "field0", "field1", "field2",
                      "field3",   "field4", "field5", "field6",
                      "field7",   "field8", "field9"};
      stats2->path = this->found.second;
      stats2->size = -1;
      stats2->valid = true;

      table->deleted_files.insert({this->found.second, std::move(stats2)});

      table->deleted_files.find(this->found.second)->second->min_val = {"0",
                                                                        "0"};
      table->deleted_files.find(this->found.second)->second->max_val = {"0",
                                                                        "0"};
      if (table->created_files.count(this->found.second) != 0) {
        table->created_files.find(this->found.second)->second->valid = false;
      }

      table->delta_log->register_operation(
          LHExecutor::LHDL::DLOperation::DELETE, this->found.second, true,
          true);

      if (table->created_files.count(this->found.second) != 0) {
        // UNDO step
        table->created_files.find(this->found.second)->second->valid = false;
      }

      if (table->deleted_files.count(this->found.second) != 0) {
        // REDO step
        table->deleted_files.find(this->found.second)->second->valid = true;
      }
    } else {
      table->delta_log->register_operation(LHExecutor::LHDL::DLOperation::ADD,
                                           argument, true, true);

      if (table->created_files.count(argument) != 0) {
        // REDO step
        table->created_files.find(argument)->second->valid = true;
      }

      if (table->deleted_files.count(argument) != 0) {
        // UNDO step
        table->deleted_files.find(argument)->second->valid = false;
      }
    }

    return true;
  }

  if (command[0] == 'd') {
    this->times_redo++;
    if (argument.find("found") != std::string::npos) {
      // read old file
      auto res = this->connector_ptr->read2(this->found.second);
      if (res->data) {
        this->big_cache = std::string(res->data, res->length);
      }
      auto stats = std::make_unique<FileStats>();
      // FileStats stats;
      stats->min_val = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
      stats->min_val = {
          this->found.first, "0", "0", "0", "0", "0", "0", "0", "0", "0", "0"};
      stats->name = {"YCSB_KEY", "field0", "field1", "field2",
                     "field3",   "field4", "field5", "field6",
                     "field7",   "field8", "field9"};
      stats->path = this->found.second;
      stats->size = -1;
      stats->valid = true;

      table->deleted_files.insert({this->found.second, std::move(stats)});

      table->deleted_files.find(this->found.second)->second->min_val = {"0",
                                                                        "0"};
      table->deleted_files.find(this->found.second)->second->max_val = {"0",
                                                                        "0"};
      if (table->created_files.count(this->found.second) != 0) {
        table->created_files.find(this->found.second)->second->valid = false;
      }

      table->delta_log->register_operation(
          LHExecutor::LHDL::DLOperation::DELETE, this->found.second, true,
          true);

      if (table->created_files.count(this->found.second) != 0) {
        // UNDO step
        table->created_files.find(this->found.second)->second->valid = false;
      }

      if (table->deleted_files.count(this->found.second) != 0) {
        // REDO step
        table->deleted_files.find(this->found.second)->second->valid = true;
      }

    } else {
      table->delta_log->register_operation(
          LHExecutor::LHDL::DLOperation::DELETE, argument, true, true);

      if (table->created_files.count(argument) != 0) {
        // UNDO step
        table->created_files.find(argument)->second->valid = false;
      }

      if (table->deleted_files.count(argument) != 0) {
        // REDO step
        table->deleted_files.find(argument)->second->valid = true;
      }
    }

    return true;
  }

  return false;
}

uint32_t LHTransactions::TransactionManagerGeneric::register_update_pairs(
    std::vector<std::pair<std::string, std::string>>& values) {
  uint32_t id = this->update_vals.size();
  update_vals.push_back(values);
  return id;
}

uint32_t LHTransactions::TransactionManagerGeneric::register_update_pairs(
    std::vector<std::pair<std::string, std::string>>& values,
    LHTransactions::UpdateOperations op,
    std::vector<std::pair<std::string, std::string>>& impl) {
  uint32_t id = this->update_vals.size();

  if (this->explicit_log) {
    update_vals.push_back(values);
  } else {
    update_vals.push_back(impl);
    this->update_ops.push_back(op);
  }

  return id;
}

bool LHTransactions::TransactionManagerGeneric::open_new_table(
    const std::string& path) {
  std::unique_ptr<TableStats> table = nullptr;
  uint32_t counter = 0;
  auto copy_path = path;
  while (!table && counter < 5) {
    table = this->open_tableV2(std::move(copy_path));
    counter++;
  }

  if (table) {
    this->tables.push_back(std::move(table));
    return true;
  }

  this->abort();
  return false;
}

void LHTransactions::TransactionManagerGeneric::print_stats() {}

uint32_t LHTransactions::TransactionManagerGeneric::get_table_id(
    std::string& key) {
  for (uint32_t i = 0; i < tables.size(); i++) {
    if (tables[i]->base_path.compare(key) == 0) {
      return i;
    }
  }
  return UINT32_MAX;
}

bool LHTransactions::TransactionManagerGeneric::parse_global_snapshots() {
  if (!this->levels[2]) {
    return true;
  }

  if (!this->connector_ptr->check(PathToGlobalLog)) {
    return false;
  }

  auto log_content = this->connector_ptr->read2(PathToGlobalLog);

  uint32_t table_id = 0;

  if (log_content->data) {
    std::stringstream stream(
        std::string(log_content->data, log_content->length));

    for (std::string line; std::getline(stream, line);) {
      if (line.size() == 0) {
        std::cerr << "17" << std::endl;
      }
      std::string path = line.substr(0, line.find(' '));
      uint32_t read_version =
          std::stoi(line.substr(line.find(' ') + 1, line.find('\n')));
      this->local_log.insert({path, read_version});
    }
  }

  return true;
}

bool LHTransactions::TransactionManagerGeneric::start_validation(
    std::unordered_map<std::string, uint32_t>& version_read_map,
    std::unordered_map<std::string, uint32_t>& version_write_map,
    std::string& content) {
  if (!this->levels[2]) {
    return true;
  }

  std::stringstream stage_conent;

  for (auto& table : this->tables) {
    uint32_t original = -1;

    if (table->deleted_files.size() != 0 || table->created_files.size() != 0) {
      version_write_map.insert({table->base_path, table->marker_version});
    } else {
      version_read_map.insert({table->base_path, table->read_version});
    }
  }

  std::srand((unsigned)time(NULL));

  stage_conent << std::rand() << " - " << version_read_map.size() << " - "
               << version_write_map.size();
  content = stage_conent.str();

  // check if another transaction is currently writing to the global log

  while (!this->connector_ptr->write_atomic(PathToGlobalTmpLog, content.c_str(),
                                            content.size())) {
  }

  bool valid = true;

  if (this->connector_ptr->check(PathToGlobalLog)) {
    auto last_snapshot_data = this->connector_ptr->read2(PathToGlobalLog);

    std::stringstream stream(
        std::string(last_snapshot_data->data, last_snapshot_data->length));

    for (std::string line; std::getline(stream, line);) {
      if (line.size() == 0) {
        std::cerr << "18" << std::endl;
      }
      std::string path = line.substr(0, line.find(' '));
      uint32_t version =
          std::stoi(line.substr(line.find(' ') + 1, line.find('\n')));

      if (version_read_map.count(path) != 0) {
        if (version > version_read_map.find(path)->second) {
          valid = false;
          break;
        }
      }

      if (version_write_map.count(path) != 0) {
        if (version >= version_write_map.find(path)->second) {
          valid = false;
          break;
        }
      } else {
        version_read_map.insert({path, version});
      }
    }
  }

  return valid;
}

bool LHTransactions::TransactionManagerGeneric::create_global_snapshot(
    std::unordered_map<std::string, uint32_t>& version_read_map,
    std::unordered_map<std::string, uint32_t>& version_write_map,
    std::string& content) {
  if (!this->levels[2]) {
    return true;
  }

  std::stringstream global_log_content;

  for (auto& ref : version_read_map) {
    if (version_write_map.count(ref.first) == 0) {
      global_log_content << ref.first << " " << ref.second << "\n";
    }
  }
  for (auto& ref : version_write_map) {
    global_log_content << ref.first << " " << ref.second << "\n";
  }

  std::string global_log_content_str = global_log_content.str();

  this->connector_ptr->write(PathToGlobalLog, global_log_content_str.c_str(),
                             global_log_content_str.size());

  this->connector_ptr->delete_file(PathToGlobalTmpLog);

  return true;
}

void LHTransactions::TransactionManagerGeneric::read_part(
    std::shared_ptr<arrow::Table>& table_ptr, std::vector<std::string>& list,
    uint32_t start, uint32_t end) {
  for (int i = start; i < end; i++) {
    auto conn_result = connector_ptr->read2(list[i]);

    auto partial_table =
        LHHelpers::readParquetAsTable(conn_result->data, conn_result->length);

    if (i != start) {
      if (table_ptr == nullptr || partial_table == nullptr) {
        std::cerr << "read error" << std::endl;
      }
      auto con_tbl = arrow::ConcatenateTables({table_ptr, partial_table});
      if (con_tbl.ok()) {
        table_ptr = *con_tbl;
      } else {
        std::cerr << "concat error" << std::endl;
      }

    } else {
      table_ptr = partial_table;
    }
  }
}

void LHTransactions::TransactionManagerGeneric::read_part_simple(
    std::vector<std::string>& list, uint32_t start, uint32_t end) {
  uint32_t counter = 0;

  for (int i = start; i < end; i++) {
    connector_ptr->read_mock(list[i]);

    counter++;
  }
}

void LHTransactions::TransactionManagerGeneric::read_part_simple_skip(
    std::vector<std::string>& list, uint32_t start, uint32_t end,
    uint32_t skip) {
  uint32_t counter = 0;
  uint32_t skip_counter = 0;

  for (int i = start; i < end; i++) {
    if (skip_counter == skip) {
      skip_counter = 0;
    } else {
      skip_counter++;
      connector_ptr->read_mock(list[i]);
    }
    counter++;
  }
}

void LHTransactions::TransactionManagerGeneric::head_part_simple(
    std::vector<std::string>& list, uint32_t start, uint32_t end) {
  uint32_t counter = 0;

  for (int i = start; i < end; i++) {
    connector_ptr->check(list[i]);

    counter++;
  }
}

std::shared_ptr<arrow::Array>
LHTransactions::TransactionManagerGeneric::build_string_col(
    std::string& name, std::shared_ptr<arrow::ChunkedArray>& old_col,
    LHTransactions::UpdateOperations op, std::string& val) {
  auto string_array =
      std::static_pointer_cast<arrow::StringArray>(old_col->chunk(0));

  auto field = std::make_shared<arrow::Field>(name, arrow::utf8());

  arrow::StringBuilder builder;
  bool update = true;
  switch (op) {
    case LHTransactions::UpdateOperations::NONE:
      update = false;
      break;
    case LHTransactions::UpdateOperations::ADD: {
      auto change = std::stof(val);
      auto old_value = std::stof(string_array->GetString(0));
      PARQUET_THROW_NOT_OK(builder.Append(std::to_string(change + old_value)));
    } break;
    case LHTransactions::UpdateOperations::SUB: {
      auto change = std::stof(val);
      auto old_value = std::stof(string_array->GetString(0));
      PARQUET_THROW_NOT_OK(builder.Append(std::to_string(old_value - change)));
    } break;
    case LHTransactions::UpdateOperations::DIV: {
      auto change = std::stof(val);
      auto old_value = std::stof(string_array->GetString(0));
      PARQUET_THROW_NOT_OK(builder.Append(std::to_string(old_value / change)));
    } break;
    case LHTransactions::UpdateOperations::MUL: {
      auto change = std::stof(val);
      auto old_value = std::stof(string_array->GetString(0));
      PARQUET_THROW_NOT_OK(builder.Append(std::to_string(old_value * change)));
    } break;
    case LHTransactions::UpdateOperations::REP: {
      PARQUET_THROW_NOT_OK(builder.Append(val));
    } break;
  }

  std::shared_ptr<arrow::Array> arrow_array;

  PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));

  return arrow_array;
}

std::shared_ptr<arrow::Array>
LHTransactions::TransactionManagerGeneric::build_int_col(
    std::string& name, std::shared_ptr<arrow::ChunkedArray>& old_col,
    LHTransactions::UpdateOperations op, int val) {
  auto int_array =
      std::static_pointer_cast<arrow::Int32Array>(old_col->chunk(0));

  auto field = std::make_shared<arrow::Field>(name, arrow::utf8());

  arrow::Int32Builder builder;
  bool update = true;
  switch (op) {
    case LHTransactions::UpdateOperations::NONE:
      update = false;
      break;
    case LHTransactions::UpdateOperations::ADD: {
      auto change = val;
      auto old_value = int_array->GetView(0);

      PARQUET_THROW_NOT_OK(builder.Append(change + old_value));
    } break;
    case LHTransactions::UpdateOperations::SUB: {
      auto change = val;
      auto old_value = int_array->GetView(0);

      PARQUET_THROW_NOT_OK(builder.Append(old_value - change));
    } break;
    case LHTransactions::UpdateOperations::DIV: {
      auto change = val;
      auto old_value = int_array->GetView(0);
      PARQUET_THROW_NOT_OK(builder.Append(old_value / change));
    } break;
    case LHTransactions::UpdateOperations::MUL: {
      auto change = val;
      auto old_value = int_array->GetView(0);
      PARQUET_THROW_NOT_OK(builder.Append(old_value * change));
    } break;
    case LHTransactions::UpdateOperations::REP: {
      PARQUET_THROW_NOT_OK(builder.Append(val));
    } break;
  }

  std::shared_ptr<arrow::Array> arrow_array;

  PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));

  return arrow_array;
}

void LHTransactions::TransactionManagerGeneric::create_checkpoint(
    uint32_t tbl_id) {
  if (tbl_id < tables.size()) {
    auto* table = tables[tbl_id].get();

    auto files = table->delta_log->get_list();

    auto checkpoint_data = LHHelpers::generateCheckpoint(files);

    std::string check_nr = std::to_string(table->delta_log->get_read_version());

    std::stringstream stream;
    stream << table->base_path << "_delta_log/";

    for (size_t i = check_nr.size(); i < 20; i++) {
      stream << "0";
    }

    stream << check_nr << ".checkpoint.parquet";

    this->connector_ptr->write(stream.str(), checkpoint_data->first,
                               checkpoint_data->second);

    std::stringstream stream2;
    stream2 << table->base_path << "_delta_log/_last_checkpoint";
  }
}
