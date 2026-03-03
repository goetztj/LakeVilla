#include "Phase.hpp"

LHLST::Phase::Phase(TpcdsPaths paths,
                    StorageConnector::MinIOConnector* connector,
                    uint32_t num_queries, uint32_t id,
                    std::vector<bool> level_config) {
  this->paths = paths;
  this->connector = connector;
  this->id = id;
  this->num_queries = num_queries;
  this->level_config = level_config;
}

LHLST::preparedLSTContent::preparedLSTContent(std::string& base_path) {
  std::ifstream sample_file;
  sample_file.open(base_path + "customer_address.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample customer_address" << std::endl;
  }

  std::string customer_address_str(
      (std::istreambuf_iterator<char>(sample_file)),
      std::istreambuf_iterator<char>());

  if (customer_address_str.empty()) {
    std::cerr << "customer_address error" << std::endl;
  }

  this->customer_address =
      LHHelpers::encodeAsParquet_str(LHHelpers::readParquetAsTable(
          customer_address_str.c_str(), customer_address_str.size()));

  sample_file.close();
  // -------------------------

  sample_file.open(base_path + "date_dim.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample date_dim" << std::endl;
  }

  std::string date_dim_str((std::istreambuf_iterator<char>(sample_file)),
                           std::istreambuf_iterator<char>());

  if (date_dim_str.empty()) {
    std::cerr << "date_dim error" << std::endl;
  }

  this->date_dim = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(date_dim_str.c_str(), date_dim_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "household_demographics.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample household_demographics" << std::endl;
  }

  std::string household_demographics_str(
      (std::istreambuf_iterator<char>(sample_file)),
      std::istreambuf_iterator<char>());

  if (household_demographics_str.empty()) {
    std::cerr << "household_demographics error" << std::endl;
  }

  this->household_demographics = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(household_demographics_str.c_str(),
                                    household_demographics_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "item.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample item" << std::endl;
  }

  std::string item_str((std::istreambuf_iterator<char>(sample_file)),
                       std::istreambuf_iterator<char>());

  if (item_str.empty()) {
    std::cerr << "item error" << std::endl;
  }

  this->item = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(item_str.c_str(), item_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "reason.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample reason" << std::endl;
  }

  std::string reason_str((std::istreambuf_iterator<char>(sample_file)),
                         std::istreambuf_iterator<char>());

  if (reason_str.empty()) {
    std::cerr << "reason error" << std::endl;
  }

  this->reason = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(reason_str.c_str(), reason_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "store.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample store" << std::endl;
  }

  std::string store_str((std::istreambuf_iterator<char>(sample_file)),
                        std::istreambuf_iterator<char>());

  if (store_str.empty()) {
    std::cerr << "store error" << std::endl;
  }

  this->store = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(store_str.c_str(), store_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "store_sales.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample store_sales" << std::endl;
  }

  std::string store_sales_str((std::istreambuf_iterator<char>(sample_file)),
                              std::istreambuf_iterator<char>());

  if (store_sales_str.empty()) {
    std::cerr << "store_sales error" << std::endl;
  }

  this->store_sales =
      LHHelpers::encodeAsParquet_str(LHHelpers::readParquetAsTable(
          store_sales_str.c_str(), store_sales_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "time_dim.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample time_dim" << std::endl;
  }

  std::string time_dim_str((std::istreambuf_iterator<char>(sample_file)),
                           std::istreambuf_iterator<char>());

  if (time_dim_str.empty()) {
    std::cerr << "time_dim error" << std::endl;
  }

  this->time_dim = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(time_dim_str.c_str(), time_dim_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "web_page.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample web_page" << std::endl;
  }

  std::string web_page_str((std::istreambuf_iterator<char>(sample_file)),
                           std::istreambuf_iterator<char>());

  if (web_page_str.empty()) {
    std::cerr << "web_page error" << std::endl;
  }

  this->web_page = LHHelpers::encodeAsParquet_str(
      LHHelpers::readParquetAsTable(web_page_str.c_str(), web_page_str.size()));

  sample_file.close();

  // -------------------------

  sample_file.open(base_path + "web_sales.parquet");

  if (!sample_file.is_open()) {
    std::cout << "Error opening sample web_sales" << std::endl;
  }

  std::string web_sales_str((std::istreambuf_iterator<char>(sample_file)),
                            std::istreambuf_iterator<char>());

  if (web_sales_str.empty()) {
    std::cerr << "web_sales error" << std::endl;
  }

  this->web_sales =
      LHHelpers::encodeAsParquet_str(LHHelpers::readParquetAsTable(
          web_sales_str.c_str(), web_sales_str.size()));

  sample_file.close();
}