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

namespace LHTPC {
struct TpccPaths {
  std::string warehouse;
  std::string stock;
  std::string item;
  std::string history;
  std::string new_order;
  std::string order_line;
  std::string district;
  std::string customer;
  std::string order;
};

// This client simulates the TPC-C accesses with the LakeVilla client
struct TpccClient {
  uint32_t aborts;

  TpccClient(std::vector<bool> levels);

  std::vector<bool> levels;

  virtual bool executeOne() = 0;

  std::shared_ptr<arrow::Table> generateHistory();

  std::shared_ptr<arrow::Table> generateOrder();

  std::shared_ptr<arrow::Table> generateNewOrder();

  std::shared_ptr<arrow::Table> generateOrderLine();
};
};  // namespace LHTPC