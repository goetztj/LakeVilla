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

namespace LHLST {

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

struct preparedLSTContent {
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

  preparedLSTContent(std::string& base_path);
};

// This is an abstract class representing a Phase of the LSTBench
struct Phase {
  TpcdsPaths paths;

  uint32_t num_queries;

  uint32_t id;

  std::vector<bool> level_config;

  StorageConnector::MinIOConnector* connector;

  Phase(TpcdsPaths paths, StorageConnector::MinIOConnector* connector,
        uint32_t num_queries, uint32_t id,
        std::vector<bool> level_config = {false, false, false});

  virtual void run(std::vector<double>& times) = 0;
};
};  // namespace LHLST