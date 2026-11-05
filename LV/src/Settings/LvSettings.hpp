#pragma once
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Connectors/MinIOConfig.hpp"
#include "LakeVilla/TransactionManager/ProtocolOptions.hpp"

namespace LHConfig {

struct LvSettings {
 public:
  std::string config_file;

  std::string host;
  std::string user;
  std::string passwd;
  std::string awsregion;
  // bucket of all table paths (default: warehouse)
  std::string bucket;
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
  // ISOLATION, MARKER_LEASE_MS, MARKER_GRACE_MS, MAX_REDOS,
  // GLOBAL_LOG_REPAIR, GLOBAL_LOG_REPAIR_AFTER, GLOBAL_LOG_CHECK_AT_BEGIN,
  // CHECKPOINTS, CHECKPOINT_INTERVAL
  LHTransactions::ProtocolOptions protocol;

  LvSettings(std::string& file);

  bool parse();

  // S3 connection of the config (HOST, USER, PASSWD, AWSREGION, BUCKET)
  StorageConnector::MinIOConfig minio_config() const;

  // makes the global-log paths and protocol options of the config the
  // default of all managers created afterwards
  void apply_defaults() const;
};

// level combination from a string such as "012" (level 0, 1 and 2)
std::vector<bool> parse_levels(const std::string& levels);

}  // namespace LHConfig