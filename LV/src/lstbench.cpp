#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/s3/S3Client.h>

#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "LSTBench/Load.hpp"
#include "LSTBench/Mixed.hpp"
#include "LSTBench/MultiUser.hpp"
#include "LSTBench/Optimize.hpp"
#include "LSTBench/SingleUser.hpp"
#include "Settings/LvSettings.hpp"

#define TPCDS_store_sales "wh/tpcds1.db/store_sales/"
#define TPCDS_reason "wh/tpcds1.db/reason/"
#define TPCDS_date_dim "wh/tpcds1.db/date_dim/"
#define TPCDS_store "wh/tpcds1.db/store/"
#define TPCDS_item "wh/tpcds1.db/item/"
#define TPCDS_household_demographics "wh/tpcds1.db/household_demographics/"
#define TPCDS_customer_address "wh/tpcds1.db/customer_address/"
#define TPCDS_web_sales "wh/tpcds1.db/web_sales/"
#define TPCDS_time_dim "wh/tpcds1.db/time_dim/"
#define TPCDS_web_page "wh/tpcds1.db/web_page/"
#define TPCDS_NUM_RUNS 3

using namespace std;

using LOAD = LHLST::Load;
using SINGLEUSER = LHLST::SingleUser;
using MULTIUSER = LHLST::MultiUser;
using MAINTENANCE = LHLST::Mixed;
using OPTIMIZE = LHLST::Optimize;
using MIXED = LHLST::Mixed;

void run(std::vector<std::unique_ptr<LHLST::Phase>>& phases) {
  std::vector<double> times;

  for (size_t i = 0; i < phases.size(); i++) {
    times.clear();
    std::cout << "----- " << i << " -----" << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    phases[i]->run(times);
    auto end = std::chrono::high_resolution_clock::now();

    phases[i] = nullptr;

    std::chrono::duration<double> duration = end - start;

    for (size_t j = 0; j < times.size(); j++) {
      std::cout << j << ", " << times[j] << std::endl;
    }
    std::cout << 60.0 * times.size() / duration.count() << "txn/min "
              << duration.count() << "s" << std::endl;
  }
}

void w0(LHLST::TpcdsPaths paths, StorageConnector::MinIOConnector* connector,
        std::vector<bool>& levels, int threads, int runs, std::string& sample) {
  auto content = std::make_unique<LHLST::preparedLSTContent>(sample);
  std::vector<std::unique_ptr<LHLST::Phase>> phases;
  std::vector<bool> levels_read = {false, false, true};
  phases.push_back(
      std::make_unique<LOAD>(paths, connector, runs, 0, content.get(), levels));

  phases.push_back(std::make_unique<SINGLEUSER>(paths, connector, runs, 1,
                                                threads, levels_read));

  auto p1 = std::make_unique<MULTIUSER>(paths);

  p1->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 2, threads,
                                             levels_read));
  p1->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 3, threads,
                                             levels_read));
  p1->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 4, threads,
                                             levels_read));
  p1->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 5, threads,
                                             levels_read));

  phases.push_back(std::move(p1));

  phases.push_back(std::make_unique<MAINTENANCE>(paths, connector, 5, 6,
                                                 content.get(), levels));

  auto p2 = std::make_unique<MULTIUSER>(paths);

  p2->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 7, threads,
                                             levels_read));
  p2->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 8, threads,
                                             levels_read));
  p2->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 9, threads,
                                             levels_read));
  p2->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 10,
                                             threads, levels_read));

  phases.push_back(std::move(p2));

  phases.push_back(std::make_unique<MAINTENANCE>(paths, connector, runs, 11,
                                                 content.get(), levels));

  run(phases);
}

void w3(LHLST::TpcdsPaths paths, StorageConnector::MinIOConnector* connector,
        std::vector<bool>& levels, int threads, int runs, std::string& sample) {
  auto content = std::make_unique<LHLST::preparedLSTContent>(sample);
  std::vector<std::unique_ptr<LHLST::Phase>> phases;
  std::vector<bool> levels_read = {false, false, true};

  // Load
  phases.push_back(
      std::make_unique<LOAD>(paths, connector, runs, 0, content.get(), levels));

  // SU, DM
  auto p1 = std::make_unique<MULTIUSER>(paths);

  p1->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 1, threads,
                                             levels_read));
  p1->add_phase(std::make_unique<MAINTENANCE>(paths, connector, runs, 2,
                                              content.get(), levels));

  phases.push_back(std::move(p1));

  // SU, O
  auto p2 = std::make_unique<MULTIUSER>(paths);

  p2->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 3, threads,
                                             levels_read));
  p2->add_phase(std::make_unique<OPTIMIZE>(paths, connector, runs, 4,
                                           content.get(), levels_read));

  phases.push_back(std::move(p2));

  // SU, DM
  auto p3 = std::make_unique<MULTIUSER>(paths);

  p3->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 5, threads,
                                             levels_read));
  p3->add_phase(std::make_unique<MAINTENANCE>(paths, connector, runs, 6,
                                              content.get(), levels));

  phases.push_back(std::move(p3));

  // DM
  phases.push_back(std::make_unique<MAINTENANCE>(paths, connector, runs, 7,
                                                 content.get(), levels));

  // SU, O
  auto p4 = std::make_unique<MULTIUSER>(paths);

  p4->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 8, threads,
                                             levels_read));
  p4->add_phase(std::make_unique<OPTIMIZE>(paths, connector, runs, 9,
                                           content.get(), levels_read));

  phases.push_back(std::move(p4));

  // SU, DM
  auto p5 = std::make_unique<MULTIUSER>(paths);

  p5->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 10,
                                             threads, levels_read));
  p5->add_phase(std::make_unique<MAINTENANCE>(paths, connector, runs, 11,
                                              content.get(), levels));

  phases.push_back(std::move(p5));

  // DM
  phases.push_back(std::make_unique<MAINTENANCE>(paths, connector, runs, 12,
                                                 content.get(), levels));

  // DM
  phases.push_back(std::make_unique<MAINTENANCE>(paths, connector, runs, 13,
                                                 content.get(), levels));

  // SU, O
  auto p6 = std::make_unique<MULTIUSER>(paths);

  p6->add_phase(std::make_unique<SINGLEUSER>(paths, connector, runs, 14,
                                             threads, levels_read));
  p6->add_phase(std::make_unique<OPTIMIZE>(paths, connector, runs, 15,
                                           content.get(), levels_read));

  phases.push_back(std::move(p6));

  run(phases);
}

void wlv(LHLST::TpcdsPaths paths, StorageConnector::MinIOConnector* connector,
         std::vector<bool>& levels, int threads, int runs,
         std::string& sample) {
  auto content = std::make_unique<LHLST::preparedLSTContent>(sample);
  std::vector<std::unique_ptr<LHLST::Phase>> phases;

  phases.push_back(std::make_unique<LHLST::Load>(paths, connector, runs, 0,
                                                 content.get(), levels));

  phases.push_back(std::make_unique<LHLST::SingleUser>(paths, connector, runs,
                                                       1, threads, levels));

  auto p1 = std::make_unique<LHLST::MultiUser>(paths);

  p1->add_phase(std::make_unique<LHLST::SingleUser>(paths, connector, runs, 2,
                                                    threads, levels));
  p1->add_phase(std::make_unique<LHLST::SingleUser>(paths, connector, runs, 3,
                                                    threads, levels));
  p1->add_phase(std::make_unique<LHLST::Mixed>(paths, connector, runs, 4,
                                               content.get(), levels));
  p1->add_phase(std::make_unique<LHLST::Mixed>(paths, connector, runs, 5,
                                               content.get(), levels));

  phases.push_back(std::move(p1));

  phases.push_back(std::make_unique<LHLST::Mixed>(paths, connector, runs, 6,
                                                  content.get(), levels));

  auto p2 = std::make_unique<LHLST::MultiUser>(paths);

  p2->add_phase(std::make_unique<LHLST::SingleUser>(paths, connector, runs, 7,
                                                    threads, levels));
  p2->add_phase(std::make_unique<LHLST::SingleUser>(paths, connector, runs, 8,
                                                    threads, levels));
  p2->add_phase(std::make_unique<LHLST::Mixed>(paths, connector, runs, 9,
                                               content.get(), levels));
  p2->add_phase(std::make_unique<LHLST::Mixed>(paths, connector, runs, 10,
                                               content.get(), levels));

  phases.push_back(std::move(p2));

  phases.push_back(std::make_unique<LHLST::Mixed>(paths, connector, 5, 11,
                                                  content.get(), levels));

  run(phases);
}

int main(const int argc, const char* argv[]) {
  if (argc != 6) {
    std::cerr << "Please provide a config file, the level combination, the "
                 "number of threads for reads, the number of runs per phase, "
                 "and the sample directory you "
                 "want to use"
              << std::endl
              << "e.g., ./lstbench-lv /path/to/config.conf 012 2 100 "
                 "'path/to/samples/"
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

  auto threads = std::stoi(std::string(argv[3]));
  auto runs = std::stoi(std::string(argv[4]));
  std::string sample = std::string(argv[5]);

  std::cout << "Welcome!\nStarting LSTBench using LakeVilla ";
  for (uint32_t i = 0; i < levels.size(); i++) {
    if (levels[i]) {
      std::cout << i;
    }
  }
  std::cout << " with " << threads << " threads." << std::endl;

  std::cout << "We currently support the following scenarios: " << std::endl
            << "[0] W0 Original TPC-DS" << std::endl
            << "[3] W3 Simultaneous sessions" << std::endl
            << "[99] WLV Extended W0 with concurrent multi-table transactions"
            << std::endl;

  std::string input_str = "";
  std::cin >> input_str;
  int input = std::stoi(input_str);

  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = Aws::Region::EU_CENTRAL_1;
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    LHLST::TpcdsPaths paths;
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

    auto connector =
        std::make_unique<StorageConnector::MinIOConnector>(&config2);

    switch (input) {
      case 0:
        w0(paths, connector.get(), levels, threads, runs, sample);
        break;
      case 3:
        w3(paths, connector.get(), levels, threads, runs, sample);
        break;
      case 99:
        wlv(paths, connector.get(), levels, threads, runs, sample);
        break;
      default:
        std::cerr << "unknown benchmark" << std::endl;
        break;
    }
  }
  Aws::ShutdownAPI(options);

  return 0;
}
