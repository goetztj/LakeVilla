#pragma once
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <streambuf>
#include <thread>
#include <utility>

#include "Helpers/Helper.hpp"
#include "LakeVilla/TransactionManager/ManagerGeneric.hpp"
#include "TpccClient.hpp"

namespace LHTPC {

// This client simulates the TPC-C accesses with the LakeVilla client
struct TpccClientSingleTable : public TpccClient {
  TpccPaths paths;

  StorageConnector::MinIOConfig config;

  TpccClientSingleTable(std::vector<bool> levels, TpccPaths paths,
                        StorageConnector::MinIOConfig config);

  bool executeOne() override;

 private:
  bool stockLevel();

  bool orderStatus();

  bool delivery();

  bool payment();

  bool newOrder();
};
};  // namespace LHTPC