#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/s3/S3Client.h>

#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Settings/LvSettings.hpp"
#include "TPC-DS/TPCDSClient.hpp"

#define TPCDS_store_sales "wh/tpcds1000.db/store_sales/"
#define TPCDS_reason "wh/tpcds1000.db/reason/"
#define TPCDS_date_dim "wh/tpcds1000.db/date_dim/"
#define TPCDS_store "wh/tpcds1000.db/store/"
#define TPCDS_item "wh/tpcds1000.db/item/"
#define TPCDS_household_demographics "wh/tpcds1000.db/household_demographics/"
#define TPCDS_customer_address "wh/tpcds1000.db/customer_address/"
#define TPCDS_web_sales "wh/tpcds1000.db/web_sales/"
#define TPCDS_time_dim "wh/tpcds1000.db/time_dim/"
#define TPCDS_web_page "wh/tpcds1000.db/web_page/"
#define TPCDS_NUM_RUNS 3

using namespace std;

int main(const int argc, const char* argv[]) {
  if (argc != 5) {
    std::cerr << "Please provide a config file, the level combination, the "
                 "number of threads, and the number of runs per query you "
                 "want to use"
              << std::endl
              << "e.g., ./tpcds-lv /path/to/config.conf 012 8 3" << std::endl;
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

  auto threads = std::stoi(std::string(argv[3]));
  auto runs = std::stoi(std::string(argv[4]));

  std::cout << "Welcome!\nStarting TPC-DS using LakeVilla ";
  for (uint32_t i = 0; i < levels.size(); i++) {
    if (levels[i]) {
      std::cout << i;
    }
  }
  std::cout << " with " << threads << " threads." << std::endl;

  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
  StorageConnector:

    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = Aws::Region::EU_CENTRAL_1;
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    LHTPCDS::TpcdsPaths paths;
    paths.customer_address = TPCDS_customer_address;
    paths.store_sales = TPCDS_store_sales;
    paths.reason = TPCDS_reason;
    paths.date_dim = TPCDS_date_dim;
    paths.store = TPCDS_store;
    paths.item = TPCDS_item;
    paths.household_demographics = TPCDS_household_demographics;
    paths.web_sales = TPCDS_web_sales;
    paths.time_dim = TPCDS_time_dim;
    paths.web_page = TPCDS_web_page;

    LHTPCDS::TPCDSClient client(paths, config2, threads);

    client.executeAll(levels, runs);
  }
  Aws::ShutdownAPI(options);

  return 0;
}
