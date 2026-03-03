#pragma once
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <streambuf>
#include <thread>
#include <utility>

#include "Connectors/MinIOConnector.hpp"
#include "Helpers/Helper.hpp"
#include "LakeVilla/TransactionManager/ManagerGeneric.hpp"

namespace LHTPCDS {
struct TpcdsPaths {
  std::string store_sales;
  std::string reason;
  std::string date_dim;
  std::string store;
  std::string item;
  std::string household_demographics;
  std::string customer_address;
  std::string web_sales;
  std::string time_dim;
  std::string web_page;
};

// This client simulates the TPC-DS accesses with the LakeVilla client
struct TPCDSClient {
  TpcdsPaths paths;

  StorageConnector::MinIOConfig config;

  std::unique_ptr<StorageConnector::MinIOConnector> connector;

  uint32_t threads;

  TPCDSClient(TpcdsPaths& paths, StorageConnector::MinIOConfig config,
              uint32_t threads);

  bool executeAll(std::vector<bool>& levels, uint32_t runs);

 private:
  bool q9(std::vector<bool>& levels);
  bool q67(std::vector<bool>& levels);
  bool q68(std::vector<bool>& levels);
  bool q90(std::vector<bool>& levels);
};
};  // namespace LHTPCDS