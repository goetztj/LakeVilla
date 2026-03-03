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

using namespace Aws;
using namespace Aws::Auth;

std::shared_ptr<arrow::Table> arrow_table_builder() {
  std::cout << "please describe the schema" << std::endl;

  bool cont = true;
  arrow::FieldVector attributes;

  arrow::ArrayVector value_vec;

  while (cont) {
    std::cout << "What is the name of the next attribute?\n";
    std::string name;
    std::cin >> name;

    std::cout << "And its data "
                 "type?\n[0]int32\n[1]float32\n[2]string\n[other]continue\n";
    std::string type;
    std::cin >> type;

    switch (std::stoi(type)) {
      case 0:
        attributes.push_back(arrow::field(name, arrow::int32()));
        break;
      case 1:
        attributes.push_back(arrow::field(name, arrow::float32()));
        break;
      case 2:
        attributes.push_back(arrow::field(name, arrow::utf8()));
        break;
      default:
        cont = false;
        break;
    }
  }

  auto schema = arrow::schema(attributes);

  std::vector<std::vector<std::string>> raw_entries;

  while (true) {
    std::cout << "Please add the next row in CSV format (separator ;). Typing "
                 "'-' continues the oepration"
              << std::endl;
    std::string row;
    std::cin >> row;

    if (row.compare("-") == 0) {
      break;
    }

    std::vector<std::string> raw_row;

    std::string value;

    std::stringstream row_stream(row);

    while (std::getline(row_stream, value, ';')) {
      std::cout << "got: " << value << std::endl;
      raw_row.push_back(value);
    }

    raw_entries.push_back(raw_row);
  }

  uint32_t col_counter = 0;
  for (auto& attr : attributes) {
    switch (attr->type()->id()) {
      case arrow::Type::INT32: {
        arrow::Int32Builder builder;
        for (auto& ref : raw_entries) {
          int32_t parsed_val = std::stoi(ref[col_counter]);
          PARQUET_THROW_NOT_OK(builder.Append(parsed_val));
        }
        std::shared_ptr<arrow::Array> arrow_array;

        PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));

        value_vec.push_back(arrow_array);
      } break;
      case arrow::Type::FLOAT: {
        arrow::FloatBuilder builder;
        for (auto& ref : raw_entries) {
          float parsed_val = std::stof(ref[col_counter]);
          PARQUET_THROW_NOT_OK(builder.Append(parsed_val));
        }
        std::shared_ptr<arrow::Array> arrow_array;

        PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));

        value_vec.push_back(arrow_array);
      } break;
      case arrow::Type::STRING: {
        arrow::StringBuilder builder;
        for (auto& ref : raw_entries) {
          std::string& parsed_val = ref[col_counter];
          PARQUET_THROW_NOT_OK(builder.Append(parsed_val));
        }
        std::shared_ptr<arrow::Array> arrow_array;

        PARQUET_THROW_NOT_OK(builder.Finish(&arrow_array));

        value_vec.push_back(arrow_array);
      } break;

      default:
        break;
    }
    col_counter++;
  }

  return arrow::Table::Make(schema, value_vec, raw_entries.size());
}

bool read_file(LHTransactions::TransactionManagerGeneric& manager) {
  std::cout << "Which table do you want to access?" << std::endl;
  std::string str_input = "";

  auto& tables = manager.tables;

  for (int i = 0; i < tables.size(); i++) {
    std::cout << "[" << i << "] " << tables[i]->base_path << std::endl;
  }

  std::cin >> str_input;

  int selection = std::stoi(str_input);

  if (selection < 0 || selection >= tables.size()) {
    std::cerr << "unknown id " << selection << std::endl;
    return true;
  }

  uint32_t table_id = tables[selection]->table_id;

  std::cout << "reading the full table (more coming soon)" << std::endl;

  auto table = manager.read_table(table_id);

  std::cout << table->ToString() << std::endl;

  return true;
}

bool add_file(LHTransactions::TransactionManagerGeneric& manager) {
  std::cout << "Which table do you want to edit?" << std::endl;
  std::string str_input = "";

  auto& tables = manager.tables;

  for (int i = 0; i < tables.size(); i++) {
    std::cout << "[" << i << "] " << tables[i]->base_path << std::endl;
  }

  std::cin >> str_input;

  int selection = std::stoi(str_input);

  if (selection < 0 || selection >= tables.size()) {
    std::cerr << "unknown id " << selection << std::endl;
    return true;
  }

  uint32_t table_id = tables[selection]->table_id;

  auto table = arrow_table_builder();
  std::string key = "";

  return manager.add_file(table_id, table, key);
}

bool delete_file(LHTransactions::TransactionManagerGeneric& manager) {
  std::cerr << "comming soon!";
  return true;
}

bool open_table(LHTransactions::TransactionManagerGeneric& manager) {
  while (true) {
    std::cout << "Please provide the path after s3a://warehouse/" << std::endl;
    std::string table_path = "";
    std::cin >> table_path;

    std::cout << "opening table at " << table_path << std::endl;

    bool success = manager.open_new_table(table_path);

    while (!success) {
      std::cout
          << "The table at " << table_path
          << " seems to have an invalid structure; Do you want to retry? (y/n)"
          << std::endl;

      std::string in;
      std::cin >> in;
      if (in.compare("n") == 0) {
        break;
      }
    }
  }
  return true;
}

bool interactive_txn(std::string& config_path) {
  std::cout << "Welcome to the interactive LakeVilla interface!" << std::endl;

  std::vector<bool> levels = {false, false, false};

  while (true) {
    std::cout << "Currently, the following levels are set: " << std::endl;
    for (int i = 0; i < levels.size(); i++) {
      std::cout << i << ": " << (levels[i] ? "active" : "inactive")
                << std::endl;
    }

    std::cout << "If you want to enable/disable a level, type its number. 3 to "
                 "continue with the transaction; 4 to abort"
              << std::endl;
    std::string in_str = "";
    std::cin >> in_str;

    if (in_str.compare("0") == 0) {
      levels[0] = !levels[0];
    } else if (in_str.compare("1") == 0) {
      levels[1] = !levels[1];
    } else if (in_str.compare("2") == 0) {
      levels[2] = !levels[2];
    } else if (in_str.compare("3") == 0) {
      break;
    } else if (in_str.compare("4") == 0) {
      return 0;
    } else {
      std::cerr << "unknown option '" << in_str << "'" << std::endl;
    }
  }

  LHConfig::LvSettings settings(config_path);
  if (!settings.parse()) {
    std::cerr << "invalid config" << std::endl;
    return 1;
  }
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "creating manager" << std::endl;
    std::string path = settings.pathToMinioTable;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    uint32_t t_id = 0;

    uint32_t counter = 0;
    uint32_t next_unique_id = 0;

    auto connector =
        std::make_unique<StorageConnector::MinIOConnector>(&config2);

    LHTransactions::TransactionManagerGeneric manager(levels, path,
                                                      connector.get(), t_id);

    std::cout << "Executing 'BEGIN TRANSACTION' ... ";
    bool opened = manager.begin_transaction_ycsb();
    while (!opened) {
      opened = manager.begin_transaction_ycsb();
      std::cerr << "Writer: failed opening table" << std::endl;
    }
    std::cout << "DONE" << std::endl;

    bool end = false;

    while (!end) {
      std::cout << "Now describe your next step (more options are comming soon)"
                << std::endl
                << "[0] read a file" << std::endl
                << "[1] add a file" << std::endl
                << "[2] remove a file" << std::endl
                << "[3] commit"
                << std::endl
                // << "[4] forced reroll" << std::endl
                << "[5] abort" << std::endl
                << "[6] open a table" << std::endl;
      std::string input;
      std::cin >> input;

      uint32_t number = std::stoi(input);

      bool result = false;

      switch (number) {
        case 0:
          result = read_file(manager);
          break;
        case 1:
          result = add_file(manager);
          break;
        case 2:
          result = delete_file(manager);
          break;
        case 3:
          result = manager.commit();
          end = true;
          break;
          // case 4:
          //   result = this->reroll();
          //   break;
        case 5:
          result = manager.abort();
          end = true;
          break;
        case 6: {
          if (!levels[1]) {
            std::cerr << "This feature requires level 1! Ignoring your command"
                      << std::endl;
          } else {
            open_table(manager);
          }
        } break;
        default:
          std::cout << "no operation" << std::endl;
          result = true;
          break;
      }

      if (!result) {
        std::cout << "Operation failed; reroll required" << std::endl;

        if (manager.reroll()) {
          std::cout << "success" << std::endl;
        } else {
          std::cout << "aborting transaction" << std::endl;
          return false;
        }
      }
    }
  }
  Aws::ShutdownAPI(options);

  return true;
}

void fresh_bench_writer(bool& found, std::string& path, LHTimer::Timer& timer,
                        LHConfig::LvSettings& config) {
  StorageConnector::MinIOConfig config2;
  config2.endpointURI = config.host;
  config2.user = config.user;
  config2.passwd = config.passwd;
  config2.region = "";
  config2.connectTimeout = 60000;
  config2.requestTimeout = 10000;
  config2.default_bucket = "warehouse";

  uint32_t t_id = 0;

  uint32_t counter = 0;
  uint32_t next_unique_id = 0;

  arrow::FieldVector attributes;

  attributes.push_back(arrow::field("FRESH_KEY", arrow::uint32()));

  auto table_schema = arrow::schema(attributes);
  std::string path2 = config.pathToFreshTable;

  while (counter < config.freshruns) {
    while (!found) {
      // wait
    }
    std::cerr << counter + 1 << "/" << config.freshruns << std::endl;
    found = false;

    LHTransactions::TransactionManagerGeneric manager({true, false, false},
                                                      path, config2, t_id);

    arrow::ArrayVector value_vec;

    arrow::UInt32Builder id_builder;
    PARQUET_THROW_NOT_OK(id_builder.Append(next_unique_id));
    std::shared_ptr<arrow::Array> arrow_id_array;

    PARQUET_THROW_NOT_OK(id_builder.Finish(&arrow_id_array));

    value_vec.push_back(std::move(arrow_id_array));

    auto table = arrow::Table::Make(table_schema, value_vec, 1);

    bool opened = manager.begin_transaction_ycsb();
    while (!opened) {
      opened = manager.begin_transaction_ycsb();
      std::cerr << "Writer: failed opening table" << std::endl;
    }
    bool sucess = false;
    timer.start();
    while (!sucess) {
      sucess = manager.add_file(0, table, std::to_string(next_unique_id));
    }
    manager.commit(false, true);

    next_unique_id++;
    counter++;
  }
}

void fresh_bench_reader(bool& found, std::vector<double>& times,
                        std::string& path, LHTimer::Timer& timer,
                        LHConfig::LvSettings& config) {
  StorageConnector::MinIOConfig config2;
  config2.endpointURI = config.host;
  config2.user = config.user;
  config2.passwd = config.passwd;
  config2.region = "";
  config2.connectTimeout = 60000;
  config2.requestTimeout = 10000;
  config2.default_bucket = "warehouse";

  uint32_t t_id = 0;

  uint32_t counter = 0;
  uint32_t next_unique_id = 0;

  found = true;
  bool first = true;

  while (counter < config.freshruns) {
    if (!first) {
      first = false;
      while (found) {
      }
    }

    LHTransactions::TransactionManagerGeneric manager({true, false, false},
                                                      path, config2, t_id);
    auto t = timer.end();
    bool opened = manager.begin_transaction_ycsb();
    while (!opened) {
      opened = manager.begin_transaction_ycsb();
      std::cerr << "Reader: failed opening table" << std::endl;
    }

    bool sucess = false;
    std::string p = "";

    auto table =
        manager.read_file_as_table(0, std::to_string(next_unique_id), p);

    if (table) {
      times.push_back(t);
      std::cout << counter << ";" << t << std::endl;
      next_unique_id++;
      counter++;
      found = true;
    }

    manager.commit(false, true);
  }
}

int fresh_bench_main(std::string& conf_path) {
  LHConfig::LvSettings settings(conf_path);
  if (!settings.parse()) {
    std::cerr << "invalid config" << std::endl;
    return 1;
  }

  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "starting freshbench" << std::endl;

    bool found = false;
    std::vector<double> times = {};
    std::string path = settings.pathToFreshTable;
    LHTimer::Timer timer;

    std::thread write_thread([&found, &path, &timer, &settings] {
      fresh_bench_writer(found, path, timer, settings);
    });

    fresh_bench_reader(found, times, path, timer, settings);

    write_thread.join();

    std::cout << "benchmark finished" << std::endl;
    uint32_t counter = 0;
    for (auto& ref : times) {
      std::cout << counter << ";" << ref << std::endl;
      counter++;
    }
  }
  Aws::ShutdownAPI(options);

  return 0;
}

void fresh_bench_writer1(bool& found, std::string& path, LHTimer::Timer& timer,
                         LHConfig::LvSettings& config) {
  StorageConnector::MinIOConfig config2;
  config2.endpointURI = config.host;
  config2.user = config.user;
  config2.passwd = config.passwd;
  config2.region = "";
  config2.connectTimeout = 60000;
  config2.requestTimeout = 10000;
  config2.default_bucket = "warehouse";

  uint32_t t_id = 0;

  uint32_t counter = 0;
  uint32_t next_unique_id = 0;

  arrow::FieldVector attributes;

  attributes.push_back(arrow::field("FRESH_KEY", arrow::uint32()));

  auto table_schema = arrow::schema(attributes);

  std::string path2 = config.pathToFreshTable;

  while (counter < config.freshruns) {
    while (!found) {
      // wait
    }
    std::cerr << counter + 1 << "/" << config.freshruns << std::endl;
    found = false;

    LHTransactions::TransactionManagerGeneric manager({true, true, false}, path,
                                                      config2, t_id);

    arrow::ArrayVector value_vec;

    arrow::UInt32Builder id_builder;
    PARQUET_THROW_NOT_OK(id_builder.Append(next_unique_id));
    std::shared_ptr<arrow::Array> arrow_id_array;

    PARQUET_THROW_NOT_OK(id_builder.Finish(&arrow_id_array));

    value_vec.push_back(std::move(arrow_id_array));

    auto table = arrow::Table::Make(table_schema, value_vec, 1);

    bool opened = manager.begin_transaction_ycsb();
    while (!opened) {
      opened = manager.begin_transaction_ycsb();
      std::cerr << "Writer: failed opening table" << std::endl;
    }
    bool sucess = false;
    timer.start();
    while (!sucess) {
      sucess = manager.add_file(0, table, std::to_string(next_unique_id));
    }

    manager.commit(false, true);

    next_unique_id++;
    counter++;
  }
}

void fresh_bench_reader1(bool& found, std::vector<double>& times,
                         std::string& path, LHTimer::Timer& timer,
                         LHConfig::LvSettings& config) {
  StorageConnector::MinIOConfig config2;
  config2.endpointURI = config.host;
  config2.user = config.user;
  config2.passwd = config.passwd;
  config2.region = "";
  config2.connectTimeout = 60000;
  config2.requestTimeout = 10000;
  config2.default_bucket = "warehouse";

  uint32_t t_id = 0;

  uint32_t counter = 0;
  uint32_t next_unique_id = 0;

  found = true;
  bool first = true;

  while (counter < config.freshruns) {
    if (!first) {
      first = false;
      while (found) {
      }
    }

    LHTransactions::TransactionManagerGeneric manager({true, true, false}, path,
                                                      config2, t_id);
    auto t = timer.end();
    bool opened = manager.begin_transaction_ycsb();
    while (!opened) {
      opened = manager.begin_transaction_ycsb();
      std::cerr << "Reader: failed opening table" << std::endl;
    }

    bool sucess = false;
    std::string p = "";

    auto table =
        manager.read_file_as_table(0, std::to_string(next_unique_id), p);

    if (table) {
      times.push_back(t);
      std::cout << counter << ";" << t << std::endl;
      next_unique_id++;
      counter++;
      found = true;
    }

    manager.commit(false, true);
  }
}

int fresh_bench_main1(std::string& conf_path) {
  LHConfig::LvSettings settings(conf_path);
  if (!settings.parse()) {
    std::cerr << "invalid config" << std::endl;
    return 1;
  }

  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "starting freshbench" << std::endl;

    bool found = false;
    std::vector<double> times = {};
    std::string path = settings.pathToFreshTable;
    LHTimer::Timer timer;

    std::thread write_thread([&found, &path, &timer, &settings] {
      fresh_bench_writer1(found, path, timer, settings);
    });

    fresh_bench_reader1(found, times, path, timer, settings);

    write_thread.join();

    std::cout << "benchmark finished" << std::endl;
    uint32_t counter = 0;
    for (auto& ref : times) {
      std::cout << counter << ";" << ref << std::endl;
      counter++;
    }
  }
  Aws::ShutdownAPI(options);

  return 0;
}

void fresh_bench_writer2(bool& found, std::string& path, LHTimer::Timer& timer,
                         LHConfig::LvSettings& config) {
  StorageConnector::MinIOConfig config2;
  config2.endpointURI = config.host;
  config2.user = config.user;
  config2.passwd = config.passwd;
  config2.region = "";
  config2.connectTimeout = 60000;
  config2.requestTimeout = 10000;
  config2.default_bucket = "warehouse";

  uint32_t t_id = 0;

  uint32_t counter = 0;
  uint32_t next_unique_id = 0;

  arrow::FieldVector attributes;

  attributes.push_back(arrow::field("FRESH_KEY", arrow::uint32()));

  auto table_schema = arrow::schema(attributes);
  std::string path2 = config.pathToFreshTable;

  while (counter < config.freshruns) {
    while (!found) {
      // wait
    }
    found = false;

    // --- build arrow table ----
    std::vector<bool> levels = {true, true, true};
    LHTransactions::TransactionManagerGeneric manager(levels, path, config2,
                                                      t_id);

    arrow::ArrayVector value_vec;

    arrow::UInt32Builder id_builder;
    PARQUET_THROW_NOT_OK(id_builder.Append(next_unique_id));
    std::shared_ptr<arrow::Array> arrow_id_array;

    PARQUET_THROW_NOT_OK(id_builder.Finish(&arrow_id_array));

    value_vec.push_back(std::move(arrow_id_array));

    auto table = arrow::Table::Make(table_schema, value_vec, 1);

    bool opened = manager.begin_transaction_ycsb();
    while (!opened) {
      opened = manager.begin_transaction_ycsb();
      std::cerr << "Writer: failed opening table" << std::endl;
    }

    bool sucess = false;
    timer.start();
    while (!sucess) {
      sucess = manager.add_file(0, table, std::to_string(next_unique_id));
    }
    manager.commit(false, true);

    next_unique_id++;
    counter++;
  }
  std::cerr << "-end" << counter << std::endl;
}

void fresh_bench_reader2(bool& found, std::vector<double>& times,
                         std::string& path, LHTimer::Timer& timer,
                         LHConfig::LvSettings& config) {
  StorageConnector::MinIOConfig config2;
  config2.endpointURI = config.host;
  config2.user = config.user;
  config2.passwd = config.passwd;
  config2.region = "";
  config2.connectTimeout = 60000;
  config2.requestTimeout = 10000;
  config2.default_bucket = "warehouse";

  uint32_t t_id = 0;

  uint32_t counter = 0;
  uint32_t next_unique_id = 0;

  found = true;
  bool first = true;

  while (counter < config.freshruns) {
    std::cerr << counter + 1 << "/" << config.freshruns << std::endl;
    if (!first) {
      while (found) {
      }
    }

    // --- build arrow table ----
    std::vector<bool> levels = {false, false, true};
    LHTransactions::TransactionManagerGeneric manager(levels, path, config2,
                                                      t_id);
    auto t = timer.end();
    bool opened = manager.begin_transaction_ycsb();
    while (!opened) {
      opened = manager.begin_transaction_ycsb();
      std::cerr << "Reader: failed opening table" << std::endl;
    }

    bool sucess = false;
    std::string p = "";

    auto table =
        manager.read_file_as_table(0, std::to_string(next_unique_id), p);

    if (table) {
      std::cerr << "found" << std::endl;
      times.push_back(t);
      std::cout << counter << ";" << t << std::endl;
      next_unique_id++;
      counter++;
      found = true;
      first = false;
    }

    manager.commit(false, true);
  }
  std::cerr << "end" << counter << std::endl;
}

int fresh_bench_main2(std::string& conf_path) {
  LHConfig::LvSettings settings(conf_path);
  if (!settings.parse()) {
    std::cerr << "invalid config" << std::endl;
    return 1;
  }

  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "starting freshbench" << std::endl;

    bool found = false;
    std::vector<double> times = {};
    std::string path = settings.pathToFreshTable;
    LHTimer::Timer timer;

    std::thread write_thread([&found, &path, &timer, &settings] {
      fresh_bench_writer2(found, path, timer, settings);
    });

    fresh_bench_reader2(found, times, path, timer, settings);

    std::cerr << "read finished" << std::endl;
    write_thread.join();
    std::cerr << "write finished" << std::endl;

    std::cout << "benchmark finished" << std::endl;
    uint32_t counter = 0;
    for (auto& ref : times) {
      std::cout << counter << ";" << ref << std::endl;
      counter++;
    }
  }
  Aws::ShutdownAPI(options);

  return 0;
}

int fresh_bench_main_simple(std::string& conf_path) {
  LHConfig::LvSettings settings(conf_path);
  if (!settings.parse()) {
    std::cerr << "invalid config" << std::endl;
    return 1;
  }
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "starting freshbench (simple)" << std::endl;
    std::vector<double> times = {};
    std::string path = settings.pathToFreshTable;
    LHTimer::Timer timer;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    uint32_t t_id = 0;

    uint32_t counter = 0;
    uint32_t next_unique_id = 0;

    arrow::FieldVector attributes;

    attributes.push_back(arrow::field("FRESH_KEY", arrow::uint32()));

    auto table_schema = arrow::schema(attributes);

    while (counter < settings.freshruns) {
      std::cerr << counter + 1 << "/" << settings.freshruns << std::endl;

      LHTransactions::TransactionManagerGeneric manager({true, false, false},
                                                        path, config2, t_id);

      arrow::ArrayVector value_vec;

      arrow::UInt32Builder id_builder;
      PARQUET_THROW_NOT_OK(id_builder.Append(next_unique_id));
      std::shared_ptr<arrow::Array> arrow_id_array;

      PARQUET_THROW_NOT_OK(id_builder.Finish(&arrow_id_array));

      value_vec.push_back(std::move(arrow_id_array));

      auto table = arrow::Table::Make(table_schema, value_vec, 1);

      bool opened = manager.begin_transaction_ycsb();
      while (!opened) {
        opened = manager.begin_transaction_ycsb();
        std::cerr << "Writer: failed opening table" << std::endl;
      }

      bool sucess = false;
      timer.start();
      while (!sucess) {
        sucess = manager.add_file(0, table, std::to_string(next_unique_id));
      }
      manager.commit(false, true);
      times.push_back(timer.end());

      next_unique_id++;
      counter++;
    }

    std::cout << "benchmark finished" << std::endl;
    uint32_t counter2 = 0;
    for (auto& ref : times) {
      std::cout << counter2 << ";" << ref << std::endl;
      counter2++;
    }
  }
  Aws::ShutdownAPI(options);

  return 0;
}

int fresh_bench_main_simple1(std::string& conf_path) {
  LHConfig::LvSettings settings(conf_path);
  if (!settings.parse()) {
    std::cerr << "invalid config" << std::endl;
    return 1;
  }
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "starting freshbench (simple)" << std::endl;
    std::vector<double> times = {};
    std::string path = settings.pathToFreshTable;
    LHTimer::Timer timer;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    uint32_t t_id = 0;

    uint32_t counter = 0;
    uint32_t next_unique_id = 0;

    arrow::FieldVector attributes;

    attributes.push_back(arrow::field("FRESH_KEY", arrow::uint32()));

    auto table_schema = arrow::schema(attributes);

    while (counter < settings.freshruns) {
      std::cerr << counter + 1 << "/" << settings.freshruns << std::endl;

      LHTransactions::TransactionManagerGeneric manager({true, true, false},
                                                        path, config2, t_id);

      arrow::ArrayVector value_vec;

      arrow::UInt32Builder id_builder;
      PARQUET_THROW_NOT_OK(id_builder.Append(next_unique_id));
      std::shared_ptr<arrow::Array> arrow_id_array;

      PARQUET_THROW_NOT_OK(id_builder.Finish(&arrow_id_array));

      value_vec.push_back(std::move(arrow_id_array));

      auto table = arrow::Table::Make(table_schema, value_vec, 1);

      bool opened = manager.begin_transaction_ycsb();
      while (!opened) {
        opened = manager.begin_transaction_ycsb();
        std::cerr << "Writer: failed opening table" << std::endl;
      }

      bool sucess = false;
      timer.start();
      while (!sucess) {
        sucess = manager.add_file(0, table, std::to_string(next_unique_id));
      }

      manager.commit(false, true);
      times.push_back(timer.end());

      next_unique_id++;
      counter++;
    }

    std::cout << "benchmark finished" << std::endl;
    uint32_t counter2 = 0;
    for (auto& ref : times) {
      std::cout << counter2 << ";" << ref << std::endl;
      counter2++;
    }
  }
  Aws::ShutdownAPI(options);

  return 0;
}

int fresh_bench_main_simple2(std::string& conf_path) {
  LHConfig::LvSettings settings(conf_path);
  if (!settings.parse()) {
    std::cerr << "invalid config" << std::endl;
    return 1;
  }
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::cout << "starting freshbench (simple)" << std::endl;
    std::vector<double> times = {};
    std::string path = settings.pathToFreshTable;
    LHTimer::Timer timer;

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    uint32_t t_id = 0;

    uint32_t counter = 0;
    uint32_t next_unique_id = 0;

    arrow::FieldVector attributes;

    attributes.push_back(arrow::field("FRESH_KEY", arrow::uint32()));

    auto table_schema = arrow::schema(attributes);

    while (counter < settings.freshruns) {
      std::cerr << counter + 1 << "/" << settings.freshruns << std::endl;

      // --- build arrow table ----
      std::vector<bool> levels = {true, true, true};
      LHTransactions::TransactionManagerGeneric manager(levels, path, config2,
                                                        t_id);

      arrow::ArrayVector value_vec;

      arrow::UInt32Builder id_builder;
      PARQUET_THROW_NOT_OK(id_builder.Append(next_unique_id));
      std::shared_ptr<arrow::Array> arrow_id_array;

      PARQUET_THROW_NOT_OK(id_builder.Finish(&arrow_id_array));

      value_vec.push_back(std::move(arrow_id_array));

      auto table = arrow::Table::Make(table_schema, value_vec, 1);

      bool opened = manager.begin_transaction_ycsb();
      while (!opened) {
        opened = manager.begin_transaction_ycsb();
        std::cerr << "Writer: failed opening table" << std::endl;
      }

      bool sucess = false;
      timer.start();
      while (!sucess) {
        sucess = manager.add_file(0, table, std::to_string(next_unique_id));
      }

      manager.commit(false, true);
      times.push_back(timer.end());

      next_unique_id++;
      counter++;
    }

    std::cout << "benchmark finished" << std::endl;
    uint32_t counter2 = 0;
    for (auto& ref : times) {
      std::cout << counter2 << ";" << ref << std::endl;
      counter2++;
    }
  }
  Aws::ShutdownAPI(options);

  return 0;
}

int main(int argc, char** argv) {
  std::cout << "Welcome!\nWhat version do you want to execute?\n[0] Freshness "
               "benchmark (lvl 0)\n[1] Freshness benchmark (lvl 0) "
               "(simplified)\n[2] Freshness benchmark (lvl 1)\n[3] Freshness "
               "benchmark (lvl 1) (simplified)\n[4] Freshness benchmark (lvl "
               "2) - coming soon\n[5] Freshness benchmark (lvl 2) "
               "(simplified)\n[6] Interactive driver V2 (incomplete)\n";

  std::string input;

  std::cin >> input;

  if (input.compare("0") == 0) {
    if (argc != 2) {
      std::cerr << "Please provide a config file for this benchamrk!"
                << std::endl;
      return 1;
    }

    std::string config_path = argv[1];

    std::cout << "Freshness benchmark" << std::endl;
    return fresh_bench_main(config_path);
  }

  if (input.compare("1") == 0) {
    if (argc != 2) {
      std::cerr << "Please provide a config file for this benchamrk!"
                << std::endl;
      return 1;
    }

    std::string config_path = argv[1];
    std::cout << "Freshness benchmark (simplified)" << std::endl;
    return fresh_bench_main_simple(config_path);
  }

  if (input.compare("2") == 0) {
    if (argc != 2) {
      std::cerr << "Please provide a config file for this benchamrk!"
                << std::endl;
      return 1;
    }

    std::string config_path = argv[1];

    std::cout << "Freshness benchmark (Level 1)" << std::endl;
    return fresh_bench_main1(config_path);
  }

  if (input.compare("3") == 0) {
    if (argc != 2) {
      std::cerr << "Please provide a config file for this benchamrk!"
                << std::endl;
      return 1;
    }

    std::string config_path = argv[1];
    std::cout << "Freshness benchmark (Level 1) (simplified)" << std::endl;
    return fresh_bench_main_simple1(config_path);
  }

  if (input.compare("4") == 0) {
    if (argc != 2) {
      std::cerr << "Please provide a config file for this benchamrk!"
                << std::endl;
      return 1;
    }

    std::cerr << "coming soon!" << std::endl;
  }

  if (input.compare("5") == 0) {
    if (argc != 2) {
      std::cerr << "Please provide a config file for this benchamrk!"
                << std::endl;
      return 1;
    }

    std::string config_path = argv[1];
    std::cout << "Freshness benchmark (Level 2) (simplified)" << std::endl;
    return fresh_bench_main_simple2(config_path);
  }

  if (input.compare("6") == 0) {
    if (argc != 2) {
      std::cerr << "Please provide a config file for the interactive driver!"
                << std::endl;
      return 1;
    }

    std::string config_path = argv[1];
    std::cout << "Interactive driver" << std::endl;
    return interactive_txn(config_path);
  }

  return 1;
}
