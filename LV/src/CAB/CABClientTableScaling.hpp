#pragma once
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <streambuf>
#include <thread>
#include <utility>

#include "CABClient.hpp"
#include "Connectors/MinIOConnector.hpp"
#include "Helpers/Helper.hpp"
#include "LakeVilla/TransactionManager/ManagerGeneric.hpp"
#include "rapidjson/include/rapidjson/document.h"
#include "rapidjson/include/rapidjson/stringbuffer.h"
#include "rapidjson/include/rapidjson/writer.h"

#define CAB_READ_PATH "./../src/CAB/cab/benchmark-query-streams/small_1tb_10cpu/streamssf1/"

namespace LHCAB {

// This client simulates the TPC-C accesses with the LakeVilla client
struct CABClientTableScaling : CABClient {
  uint32_t max_tables;
  std::string scale_table;

  CABClientTableScaling(uint32_t max_tables, std::string scale_table,
                        std::string& base_path, uint64_t tendant,
                        StorageConnector::MinIOConfig config, uint32_t id,
                        preparedContent* content);

  bool startAll(std::vector<std::vector<bool>> levels,
                std::vector<bool>& triggers, bool& end,
                std::vector<std::vector<std::pair<int64_t, int64_t>>>& times,
                double& reads_mb, double& writes_mb);

  bool executeMultiTableTransaction(
      uint32_t runs, uint32_t num_tables,
      std::vector<std::pair<int64_t, int64_t>>& times);
};
};  // namespace LHCAB