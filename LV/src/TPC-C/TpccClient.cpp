#include "TpccClient.hpp"

LHTPC::TpccClient::TpccClient(std::vector<bool> levels) {
  this->levels = levels;
  this->aborts = 0;
}

std::shared_ptr<arrow::Table> LHTPC::TpccClient::generateHistory() {
  std::ifstream sample_file;
  sample_file.open("/LakeVilla/tpcc/sample/history.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample" << std::endl;
  }

  std::string history_str((std::istreambuf_iterator<char>(sample_file)),
                          std::istreambuf_iterator<char>());

  auto table =
      LHHelpers::readParquetAsTable(history_str.c_str(), history_str.size());

  sample_file.close();
  return table;
}

std::shared_ptr<arrow::Table> LHTPC::TpccClient::generateOrder() {
  std::ifstream sample_file;
  sample_file.open("/LakeVilla/tpcc/sample/order.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample" << std::endl;
  }

  std::string order_str((std::istreambuf_iterator<char>(sample_file)),
                        std::istreambuf_iterator<char>());

  auto table =
      LHHelpers::readParquetAsTable(order_str.c_str(), order_str.size());

  sample_file.close();
  return table;
}

std::shared_ptr<arrow::Table> LHTPC::TpccClient::generateNewOrder() {
  std::ifstream sample_file;
  sample_file.open("/LakeVilla/tpcc/sample/new_order.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample" << std::endl;
  }

  std::string new_order_str((std::istreambuf_iterator<char>(sample_file)),
                            std::istreambuf_iterator<char>());

  auto table = LHHelpers::readParquetAsTable(new_order_str.c_str(),
                                             new_order_str.size());

  sample_file.close();
  return table;
}

std::shared_ptr<arrow::Table> LHTPC::TpccClient::generateOrderLine() {
  std::ifstream sample_file;
  sample_file.open("/LakeVilla/tpcc/sample/order_line.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample" << std::endl;
  }

  std::string order_line_str((std::istreambuf_iterator<char>(sample_file)),
                             std::istreambuf_iterator<char>());

  auto table = LHHelpers::readParquetAsTable(order_line_str.c_str(),
                                             order_line_str.size());

  sample_file.close();
  return table;
}