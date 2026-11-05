#include <aws/core/Aws.h>

#include <atomic>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/s3/S3Client.h>

#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Settings/LvSettings.hpp"
#include "TPC-C/TpccClientMultiTable.hpp"
#include "TPC-C/TpccClientSingleTable.hpp"

#define TPCC_WAREHOUSE "wh/tpcc_sf1.db/warehouse/"
#define TPCC_STOCK "wh/tpcc_sf1.db/stock/"
#define TPCC_ITEM "wh/tpcc_sf1.db/item/"
#define TPCC_HISTORY "wh/tpcc_sf1.db/history/"
#define TPCC_NEW_ORDER "wh/tpcc_sf1.db/new_order/"
#define TPCC_ORDER_LINE "wh/tpcc_sf1.db/order_line/"
#define TPCC_DISTRICT "wh/tpcc_sf1.db/district/"
#define TPCC_CUSTOMER "wh/tpcc_sf1.db/customer/"
#define TPCC_ORDER "wh/tpcc_sf1.db/order/"
#define TPCC_NUM_RUNS 500

using namespace std;

void exec(int id, std::vector<bool> levels, LHTPC::TpccPaths paths,
          StorageConnector::MinIOConfig config2,
          std::atomic<uint64_t>& committed) {
  std::unique_ptr<LHTPC::TpccClient> client = nullptr;

  if (!levels[1] && !levels[2]) {
    client =
        std::make_unique<LHTPC::TpccClientSingleTable>(levels, paths, config2);
    std::cerr << " (single table)" << std::endl;
  } else {
    client =
        std::make_unique<LHTPC::TpccClientMultiTable>(levels, paths, config2);
    std::cerr << " (multi table)" << std::endl;
  }

  std::vector<double> times;

  for (int i = 0; i < TPCC_NUM_RUNS; i++) {
    auto start = std::chrono::steady_clock::now();
    if (client->executeOne()) {
      committed++;
      auto end = std::chrono::steady_clock::now();
      std::cerr << i + 1 << "/" << TPCC_NUM_RUNS << std::endl;
      std::chrono::duration<double> duration = end - start;
      times.push_back(duration.count());
    } else {
      std::cerr << "abort" << std::endl;
    }
  }

  std::ofstream file;
  std::stringstream stream;
  stream << "./" << id << ".csv";
  file.open(stream.str());

  for (size_t i = 0; i < times.size(); i++) {
    file << i << "; " << times[i] << std::endl;
  }

  file << "aborts: " << client->aborts << std::endl;

  file.close();
}

int main(const int argc, const char* argv[]) {
  if (argc != 4) {
    std::cerr << "Please provide a config file the level combination, and "
                 "clients you "
                 "want to use"
              << std::endl
              << "e.g., ./tpcc-lv /path/to/config.conf 012 1" << std::endl;
    return 1;
  }

  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    std::string config_path = argv[1];
    LHConfig::LvSettings settings(config_path);
    if (!settings.parse()) {
      std::cerr << "invalid config" << std::endl;
      Aws::ShutdownAPI(options);
      return 1;
    }
    settings.apply_defaults();

    auto levels = LHConfig::parse_levels(argv[2]);

    auto clients = std::stoi(argv[3]);

    StorageConnector::MinIOConfig config2 = settings.minio_config();

    LHTPC::TpccPaths paths;
    paths.warehouse = TPCC_WAREHOUSE;
    paths.stock = TPCC_STOCK;
    paths.item = TPCC_ITEM;
    paths.history = TPCC_HISTORY;
    paths.new_order = TPCC_NEW_ORDER;
    paths.order_line = TPCC_ORDER_LINE;
    paths.district = TPCC_DISTRICT;
    paths.customer = TPCC_CUSTOMER;
    paths.order = TPCC_ORDER;

    std::cerr << "using ";
    auto counter = 0;
    for (auto ref : levels) {
      if (ref) {
        std::cerr << counter;
      }
      counter++;
    }

    std::cerr << " (" << clients << ")" << std::endl;

    std::vector<std::thread> threads;

    auto start = std::chrono::steady_clock::now();

    std::atomic<uint64_t> committed = 0;
    for (int i = 0; i < clients; i++) {
      // i by value: each client has its own id and CSV file
      threads.emplace_back(
          [&, i] { exec(i, levels, paths, config2, committed); });
    }

    for (auto& t : threads) {
      t.join();
    }

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> duration = end - start;

    uint64_t attempted = uint64_t(clients) * TPCC_NUM_RUNS;
    std::cout << 1.0 * committed / duration.count() << " txn/s in "
              << duration.count() << "s (" << committed << " of " << attempted
              << " transactions committed)" << std::endl;
  }
  Aws::ShutdownAPI(options);

  return 0;
}
