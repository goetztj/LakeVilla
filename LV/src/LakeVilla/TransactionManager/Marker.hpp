#pragma once
#include <ctime>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>

#include "Connectors/MinIOConnector.hpp"
#include "Helpers/Helper.hpp"

namespace LHTransactions {

struct Marker {
  std::string marker_path;

  Marker(std::string&& marker_path,
         StorageConnector::MinIOConnector* connector_ptr);
  ~Marker();

  void setInvalid();

 private:
  bool valid;

  StorageConnector::MinIOConnector* connector_ptr;

  void createHeaderCommit(std::string& out_string);
};
};  // namespace LHTransactions