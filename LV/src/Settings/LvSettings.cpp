#include "LvSettings.hpp"

#include "LakeVilla/TransactionManager/Manager.hpp"

#include <algorithm>
#include <cctype>

namespace {
std::string trim(const std::string& in) {
  auto begin = std::find_if_not(in.begin(), in.end(), [](unsigned char c) {
    return std::isspace(c);
  });
  auto end = std::find_if_not(in.rbegin(), in.rend(), [](unsigned char c) {
               return std::isspace(c);
             }).base();
  return begin < end ? std::string(begin, end) : std::string();
}

// parses 0/1/true/false; keeps 'target' and reports invalid values
void parse_flag(const std::string& property, const std::string& value,
                bool& target) {
  if (value == "1" || value == "true") {
    target = true;
  } else if (value == "0" || value == "false") {
    target = false;
  } else {
    std::cerr << "invalid flag '" << value << "' for " << property
              << "; ignoring" << std::endl;
  }
}

// parses a non-negative number; keeps 'target' and reports invalid values
void parse_number(const std::string& property, const std::string& value,
                  uint32_t& target) {
  try {
    size_t used = 0;
    auto parsed = std::stoul(value, &used);
    if (used != value.size()) {
      throw std::invalid_argument(value);
    }
    target = static_cast<uint32_t>(parsed);
  } catch (const std::exception&) {
    std::cerr << "invalid number '" << value << "' for " << property
              << "; ignoring" << std::endl;
  }
}
}  // namespace

LHConfig::LvSettings::LvSettings(std::string& file) {
  this->config_file = file;

  host = "";
  user = "";
  passwd = "";
  awsregion = "";
  bucket = "warehouse";
  warmupruns = 0;
  benchmarkruns = 0;
  cooldownruns = 0;
  pathToMinioTable = "";
  pathToGlobalLog = "";
  pathToGlobalTmpLog = "";
  pathToYCSBTable = "";
  pathToBankingTable0 = "";
  pathToBankingTable1 = "";
  logthreads = 1;
  freshruns = 0;
  pathToFreshTable = "";
  cab_path = "";
  readthreads = 1;
}

bool LHConfig::LvSettings::parse() {
  std::cerr << "parsing config file " << this->config_file << std::endl;

  std::ifstream file;
  file.open(this->config_file);
  if (!file.is_open()) {
    std::cerr << "error opening config file at " << this->config_file
              << std::endl;
    return false;
  }

  std::string line = "";

  while (std::getline(file, line, '\n')) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    auto pos = line.find('=');
    if (pos == std::string::npos) {
      std::cerr << "invalid line " << line << " in config file; ignoring"
                << std::endl;
      continue;
    }

    auto property = trim(line.substr(0, pos));
    auto value = trim(line.substr(pos + 1));

    if (property == "HOST") {
      this->host = value;
    } else if (property == "USER") {
      this->user = value;
    } else if (property == "PASSWD") {
      this->passwd = value;
    } else if (property == "AWSREGION") {
      this->awsregion = value;
    } else if (property == "BUCKET") {
      this->bucket = value;
    } else if (property == "PathToMinioTable") {
      this->pathToMinioTable = value;
    } else if (property == "PathToGlobalLog") {
      this->pathToGlobalLog = value;
    } else if (property == "PathToGlobalTmpLog") {
      this->pathToGlobalTmpLog = value;
    } else if (property == "PathToYCSBTable") {
      this->pathToYCSBTable = value;
    } else if (property == "PathToBankingTable0") {
      this->pathToBankingTable0 = value;
    } else if (property == "PathToBankingTable1") {
      this->pathToBankingTable1 = value;
    } else if (property == "PathToFreshTable") {
      this->pathToFreshTable = value;
    } else if (property == "WARMUPRUNS") {
      parse_number(property, value, this->warmupruns);
    } else if (property == "BENCHMARKRUNS") {
      parse_number(property, value, this->benchmarkruns);
    } else if (property == "COOLDOWNRUNS") {
      parse_number(property, value, this->cooldownruns);
    } else if (property == "CAB_PATH") {
      this->cab_path = value;
    } else if (property == "LOGTHREADS") {
      parse_number(property, value, this->logthreads);
      if (this->logthreads == 0) {
        std::cerr << "You must provide at least one thread for log parsing; "
                     "ignoring: "
                  << line << std::endl;
        this->logthreads = 1;
      }
    } else if (property == "FRESHRUNS") {
      parse_number(property, value, this->freshruns);
    } else if (property == "READTHREADS") {
      parse_number(property, value, this->readthreads);
    } else if (property == "ISOLATION") {
      if (value == "serializable") {
        this->protocol.isolation = LHTransactions::Isolation::Serializable;
      } else if (value == "snapshot") {
        this->protocol.isolation = LHTransactions::Isolation::Snapshot;
      } else {
        std::cerr << "invalid ISOLATION '" << value
                  << "' (serializable, snapshot); ignoring" << std::endl;
      }
    } else if (property == "MARKER_LEASE_MS") {
      parse_number(property, value, this->protocol.marker_lease_ms);
    } else if (property == "MARKER_GRACE_MS") {
      parse_number(property, value, this->protocol.marker_grace_ms);
    } else if (property == "MAX_REDOS") {
      parse_number(property, value, this->protocol.max_redos);
    } else if (property == "GLOBAL_LOG_REPAIR") {
      parse_flag(property, value, this->protocol.global_log_repair);
    } else if (property == "GLOBAL_LOG_REPAIR_AFTER") {
      parse_number(property, value, this->protocol.global_log_repair_after);
    } else if (property == "GLOBAL_LOG_CHECK_AT_BEGIN") {
      parse_flag(property, value, this->protocol.global_log_check_at_begin);
    } else if (property == "CHECKPOINTS") {
      parse_flag(property, value, this->protocol.checkpoints);
    } else if (property == "CHECKPOINT_INTERVAL") {
      parse_number(property, value, this->protocol.checkpoint_interval);
    } else {
      std::cerr << "unknown property " << property << "; ignoring"
                << std::endl;
    }
  }
  std::cerr << "parsing done" << std::endl;
  return true;
}

StorageConnector::MinIOConfig LHConfig::LvSettings::minio_config() const {
  StorageConnector::MinIOConfig config;
  config.endpointURI = this->host;
  config.user = this->user;
  config.passwd = this->passwd;
  config.region = this->awsregion;
  config.connectTimeout = 60000;
  config.requestTimeout = 10000;
  config.default_bucket = this->bucket;
  return config;
}

void LHConfig::LvSettings::apply_defaults() const {
  if (!this->pathToGlobalLog.empty() && !this->pathToGlobalTmpLog.empty()) {
    LHTransactions::set_default_global_log_paths(this->pathToGlobalLog,
                                                 this->pathToGlobalTmpLog);
  }
  LHTransactions::set_default_protocol_options(this->protocol);
}

std::vector<bool> LHConfig::parse_levels(const std::string& levels) {
  std::vector<bool> result = {false, false, false};
  for (char level : levels) {
    if (level >= '0' && level <= '2') {
      result[level - '0'] = true;
    } else {
      std::cerr << "ignoring unknown level '" << level << "'" << std::endl;
    }
  }
  return result;
}
