#pragma once
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace LHConfig {

struct LvSettings {
 public:
  std::string config_file;

  std::string host;
  std::string user;
  std::string passwd;
  std::string awsregion;
  uint32_t warmupruns;
  uint32_t benchmarkruns;
  uint32_t cooldownruns;
  std::string pathToMinioTable;
  std::string pathToGlobalLog;
  std::string pathToGlobalTmpLog;
  std::string pathToYCSBTable;
  std::string pathToBankingTable0;
  std::string pathToBankingTable1;
  uint32_t logthreads;
  uint32_t freshruns;
  std::string pathToFreshTable;
  std::string cab_path;
  uint32_t readthreads;

  LvSettings(std::string& file);

  bool parse();
};

}  // namespace LHConfig