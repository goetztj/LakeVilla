#pragma once

#include <cstdint>
#include <string>

#include "Connector.hpp"

namespace StorageConnector {

struct MinIOConfig {
  std::string user;
  std::string passwd;
  std::string region;
  bool verifySSL = false;
  uint64_t connectTimeout = 60000;
  uint64_t requestTimeout = 10000;
  std::string endpointURI;
  bool use_https = false;
  std::string default_bucket = "";

  MinIOConfig() = default;

  ConnectorType getType() { return StorageConnector::ConnectorType::MinIO; };
};

}  // namespace StorageConnector
