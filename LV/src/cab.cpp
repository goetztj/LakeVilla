#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/s3/S3Client.h>

#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "CAB/CABClientReadSwitching.hpp"
#include "CAB/CABClientTableScaling.hpp"
#include "CAB/CABClientWriteSwitching.hpp"
#include "Settings/LvSettings.hpp"
#include "settings.hpp"

#define CAB_PATH "wh/"
#define CAB_CLEANUP_TIME 1800

using namespace std;

int table_scaling(LHConfig::LvSettings& settings, std::string& sample_path,
                  uint32_t num_tables, std::string& scale_table) {
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    bool mainStart2 = false;
    bool mainStart4 = false;
    bool mainStart6 = false;
    bool mainStart8 = false;

    std::string base_path = settings.cab_path;

    auto prepCont = std::make_unique<LHCAB::preparedContent>(sample_path);

    std::vector<std::vector<std::pair<int64_t, int64_t>>> times;

    double reads_2_w;
    double writes_2_w;

    std::vector<std::vector<bool>> levels;
    levels.push_back({false, false, true});

    std::vector<bool> triggers;
    triggers.reserve(1);
    for (size_t i = 0; i < 1; i++) {
      triggers.push_back(false);
    }

    bool end_bench = false;

    auto table_client = std::make_unique<LHCAB::CABClientTableScaling>(
        num_tables, scale_table, base_path, 1, config2, 1, prepCont.get());

    table_client->startAll(levels, triggers, end_bench, times, reads_2_w,
                           writes_2_w);
  }
  Aws::ShutdownAPI(options);
  return 0;
}

int level_switching_workload(LHConfig::LvSettings& settings,
                             std::string& sample_path,
                             uint32_t num_read_clients,
                             uint32_t num_write_clients) {
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    std::string base_path = settings.cab_path;

    auto prepCont = std::make_unique<LHCAB::preparedContent>(sample_path);

    std::cout << "creating global version log" << std::endl;

    auto client_clean = std::make_unique<LHCAB::CABClientWriteSwitching>(
        1, base_path, 1, config2, 0, prepCont.get());
    client_clean->executeIngestionEnd();

    std::vector<std::vector<std::pair<int64_t, int64_t>>> times_read,
        times_write;

    double reads_2_w;
    double writes_2_w;

    std::vector<std::vector<bool>> levels_write, levels_read;
    levels_write.reserve(3);
    levels_read.reserve(3);
    levels_write.push_back({false, false, false});
    levels_write.push_back({true, false, false});
    levels_write.push_back({true, true, false});
    levels_read.push_back({false, false, false});
    levels_read.push_back({false, false, true});
    levels_read.push_back({false, true, true});

    std::vector<bool> triggers;
    triggers.reserve(3);
    for (size_t i = 0; i < 3; i++) {
      triggers.push_back(false);
    }

    bool end_bench = false;

    std::thread read_thread([&] {
      auto read_client = std::make_unique<LHCAB::CABClientReadSwitching>(
          num_read_clients, base_path, 1, config2, 0, prepCont.get());
      read_client->startAll(levels_read, triggers, end_bench, times_read,
                            reads_2_w, reads_2_w);
    });

    std::thread write_thread([&] {
      auto write_client = std::make_unique<LHCAB::CABClientWriteSwitching>(
          num_write_clients, base_path, 1, config2, 0, prepCont.get());
      write_client->startAll(levels_write, triggers, end_bench, times_write,
                             reads_2_w, reads_2_w);
    });

    std::cerr << "waiting 2min for all threads" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(60 * 2));

    for (uint32_t p = 0; p < triggers.size(); p++) {
      triggers[p] = true;
      std::cerr << "----- Phase " << p << " -----" << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(60 * 20));
    }

    end_bench = true;

    std::cerr << "workload stopped; waiting until all threads are done"
              << std::endl;
    read_thread.join();
    write_thread.join();

    std::cout << "<<<<< Reads >>>>>" << std::endl;
    for (uint32_t p = 0; p < triggers.size(); p++) {
      std::cout << "----- Phase " << p << " -----" << std::endl;
      for (auto& res : times_read[p]) {
        std::cout << res.first << ";" << res.second << std::endl;
      }
      std::cout << "----------------" << std::endl;
    }

    std::cout << "<<<<< Writes >>>>>" << std::endl;
    for (uint32_t p = 0; p < triggers.size(); p++) {
      std::cout << "----- Phase " << p << " -----" << std::endl;
      for (auto& res : times_write[p]) {
        std::cout << res.first << ";" << res.second << std::endl;
      }
      std::cout << "----------------" << std::endl;
    }
  }
  Aws::ShutdownAPI(options);
  return 0;
}

int level_switching_workload_write(LHConfig::LvSettings& settings,
                                   std::string& sample_path,
                                   uint32_t num_clients) {
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    bool mainStart2 = false;
    bool mainStart4 = false;
    bool mainStart6 = false;
    bool mainStart8 = false;

    std::string base_path = settings.cab_path;

    auto prepCont = std::make_unique<LHCAB::preparedContent>(sample_path);

    std::cout << "creating global version log" << std::endl;

    auto client_clean = std::make_unique<LHCAB::CABClientWriteSwitching>(
        1, base_path, 1, config2, 0, prepCont.get());
    client_clean->executeIngestionEnd();

    std::vector<std::vector<std::pair<int64_t, int64_t>>> times;

    double reads_2_w;
    double writes_2_w;

    std::vector<std::vector<bool>> levels;
    levels.reserve(1);
    // levels.push_back({false, false, false});
    // levels.push_back({true, false, false});
    // levels.push_back({true, true, false});
    // levels.push_back({true, true, true});
    // levels.push_back({false, true, false});
    // levels.push_back({false, true, true});
    levels.push_back({false, false, true});

    std::vector<bool> triggers;
    triggers.reserve(1);
    for (size_t i = 0; i < 1; i++) {
      triggers.push_back(false);
    }

    bool end_bench = false;

    std::thread write_thread([&] {
      auto write_client = std::make_unique<LHCAB::CABClientWriteSwitching>(
          1, base_path, 1, config2, 0, prepCont.get());
      write_client->startAll(levels, triggers, end_bench, times, reads_2_w,
                             reads_2_w);
    });

    std::cerr << "waiting 2min for all threads" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(30));

    for (uint32_t p = 0; p < triggers.size(); p++) {
      triggers[p] = true;
      std::cerr << "----- Phase " << p << " -----" << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(60 * 10));
    }

    end_bench = true;

    std::cerr << "workload stopped; waiting until all threads are done"
              << std::endl;
    write_thread.join();

    for (uint32_t p = 0; p < triggers.size(); p++) {
      std::cout << "----- Phase " << p << " -----" << std::endl;
      for (auto& res : times[p]) {
        std::cout << res.first << ";" << res.second << std::endl;
      }
      std::cout << "----------------" << std::endl;
    }
  }
  Aws::ShutdownAPI(options);
  return 0;
}

int level_switching_workload_read(LHConfig::LvSettings& settings,
                                  std::string& sample_path,
                                  uint32_t num_clients) {
  Aws::SDKOptions options;
  Aws::InitAPI(options);
  {
    StorageConnector::MinIOConfig config2;
    config2.endpointURI = settings.host;
    config2.user = settings.user;
    config2.passwd = settings.passwd;
    config2.region = "";
    config2.connectTimeout = 60000;
    config2.requestTimeout = 10000;
    config2.default_bucket = "warehouse";

    bool mainStart2 = false;
    bool mainStart4 = false;
    bool mainStart6 = false;
    bool mainStart8 = false;

    std::string base_path = settings.cab_path;

    auto prepCont = std::make_unique<LHCAB::preparedContent>(sample_path);

    std::cout << "creating global version log" << std::endl;

    auto client_clean = std::make_unique<LHCAB::CABClientReadSwitching>(
        1, base_path, 1, config2, 0, prepCont.get());
    client_clean->executeIngestionEnd();

    std::vector<std::vector<std::pair<int64_t, int64_t>>> times;

    double reads_2_w;
    double writes_2_w;

    std::vector<std::vector<bool>> levels;
    levels.reserve(7);
    levels.push_back({false, false, false});
    levels.push_back({true, false, false});
    levels.push_back({true, true, false});
    levels.push_back({true, true, true});
    levels.push_back({false, true, false});
    levels.push_back({false, true, true});
    levels.push_back({false, false, true});

    std::vector<bool> triggers;
    triggers.reserve(7);
    for (size_t i = 0; i < 7; i++) {
      triggers.push_back(false);
    }

    bool end_bench = false;

    std::thread read_thread([&] {
      auto read_client = std::make_unique<LHCAB::CABClientReadSwitching>(
          num_clients, base_path, 1, config2, 0, prepCont.get());
      read_client->startAll(levels, triggers, end_bench, times, reads_2_w,
                            reads_2_w);
    });

    std::cerr << "waiting 2min for all threads" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(60 * 2));

    for (uint32_t p = 0; p < triggers.size(); p++) {
      triggers[p] = true;
      std::cerr << "----- Phase " << p << " -----" << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(60 * 10));
      std::cerr << "----- Phase " << p << " end -----" << std::endl;
    }

    std::cerr << "stopping workload" << std::endl;

    end_bench = true;

    std::cerr << "workload stopped; waiting until all threads are done"
              << std::endl;
    read_thread.join();

    for (uint32_t p = 0; p < triggers.size(); p++) {
      std::cout << "----- Phase " << p << " -----" << std::endl;
      for (auto& res : times[p]) {
        std::cout << res.first << ";" << res.second << std::endl;
      }
      std::cout << "----------------" << std::endl;
    }
  }
  Aws::ShutdownAPI(options);
  return 0;
}

int main(const int argc, const char* argv[]) {
  if (argc != 4 && argc != 6) {
    std::cerr
        << "please provide all required data: ./cab-lv <type> <config> <sample>"
        << std::endl
        << "or ./cab-lv mixed <config> <sample> <num_readers> <num_writes>"
        << std::endl
        << "or ./cab-lv table <config> <sample> <num_tables> <scale_table_path>"
        << std::endl;

    return 1;
  }

  std::string type = argv[1];
  std::string conf_path = argv[2];
  std::string sample_path = argv[3];

  LHConfig::LvSettings settings(conf_path);
  if (!settings.parse()) {
    std::cerr << "invalid config" << std::endl;
    return 1;
  }

  if (type.compare("read") == 0) {
    std::cerr << "executing levels switiching workload (reads)" << std::endl;
    return level_switching_workload_read(settings, sample_path, 1);
  }

  if (type.compare("write") == 0) {
    std::cerr << "executing levels switiching workload (writes)" << std::endl;
    return level_switching_workload_write(settings, sample_path, 1);
  }

  if (type.compare("table") == 0) {
    std::cerr << "executing table scaling" << std::endl;
    uint32_t num_tables = std::stoi(argv[4]);
    std::string scale_path = argv[5];
    return table_scaling(settings, sample_path, num_tables, scale_path);
  }

  if (type.compare("mixed") == 0) {
    std::cerr << "executing complete workload" << std::endl;
    uint32_t read_clients = std::stoi(argv[4]);
    uint32_t write_clients = std::stoi(argv[5]);
    return level_switching_workload(settings, sample_path, read_clients,
                                    write_clients);
  }

  return 0;
}
