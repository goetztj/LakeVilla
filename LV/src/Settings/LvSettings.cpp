#include "LvSettings.hpp"

LHConfig::LvSettings::LvSettings(std::string& file) {
  this->config_file = file;

  host = "";
  user = "";
  passwd = "";
  awsregion = "";
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
    auto pos = line.find('=');
    if (pos == std::string::npos) {
      std::cerr << "invalid line " << line << " in config file; ignoring"
                << std::endl;
    } else {
      auto property = line.substr(0, pos);
      auto value = line.substr(pos + 1);

      // std::cerr << "prop: "<< property << " val: " << value << std::endl;

      if (property.compare("HOST") == 0) {
        this->host = value;
      }

      if (property.compare("USER") == 0) {
        this->user = value;
      }

      if (property.compare("PASSWD") == 0) {
        this->passwd = value;
      }

      if (property.compare("AWSREGION") == 0) {
        this->awsregion = value;
      }

      if (property.compare("PathToMinioTable") == 0) {
        this->pathToMinioTable = value;
      }

      if (property.compare("PathToGlobalLog") == 0) {
        this->pathToGlobalLog = value;
      }

      if (property.compare("PathToGlobalTmpLog") == 0) {
        this->pathToGlobalTmpLog = value;
      }

      if (property.compare("PathToYCSBTable") == 0) {
        this->pathToYCSBTable = value;
      }

      if (property.compare("PathToBankingTable0") == 0) {
        this->pathToBankingTable0 = value;
      }

      if (property.compare("PathToBankingTable1") == 0) {
        this->pathToBankingTable1 = value;
      }

      if (property.compare("PathToFreshTable") == 0) {
        this->pathToFreshTable = value;
      }

      if (property.compare("WARMUPRUNS") == 0) {
        this->warmupruns = std::stoi(value);
      }

      if (property.compare("BENCHMARKRUNS") == 0) {
        this->benchmarkruns = std::stoi(value);
      }

      if (property.compare("COOLDOWNRUNS") == 0) {
        this->cooldownruns = std::stoi(value);
      }

      if (property.compare("CAB_PATH") == 0) {
        this->cab_path = value;
      }

      if (property.compare("LOGTHREADS") == 0) {
        this->logthreads = std::stoi(value);
        if (this->logthreads <= 0) {
          std::cerr << "Ypu must provide at least one thread for log parsing; "
                       "ignoring: "
                    << line << std::endl;
          this->logthreads = 1;
        }
      }

      if (property.compare("FRESHRUNS") == 0) {
        this->freshruns = std::stoi(value);
      }

      if (property.compare("READTHREADS") == 0) {
        this->readthreads = std::stoi(value);
      }
    }
  }
  std::cerr << "parsing done" << std::endl;
  return true;
}