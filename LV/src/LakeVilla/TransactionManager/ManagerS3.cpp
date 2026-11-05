// Constructors that create their own S3 (MinIO) connector. They live in
// lakevilla-s3 so that the core library does not depend on the AWS SDK.
#include "Connectors/MinIOConnector.hpp"
#include "LakeVilla/TransactionManager/ManagerDeltaLake.hpp"

LHTransactions::TransactionManager::TransactionManager(
    std::string& path_to_table, StorageConnector::MinIOConfig config,
    uint32_t transaction_id)
    : TransactionManager(path_to_table,
                         static_cast<StorageConnector::Connector*>(nullptr),
                         transaction_id) {
  this->connector = std::make_unique<StorageConnector::MinIOConnector>(&config);
  this->connector_ptr = this->connector.get();
}

LHTransactions::TransactionManagerDeltaLake::TransactionManagerDeltaLake(
    LHTransactions::LakeVillaConf levels, std::string& path_to_table,
    StorageConnector::MinIOConfig config, uint32_t transaction_id)
    : TransactionManagerDeltaLake(
          levels, path_to_table,
          static_cast<StorageConnector::Connector*>(nullptr), transaction_id) {
  this->connector = std::make_unique<StorageConnector::MinIOConnector>(&config);
  this->connector_ptr = this->connector.get();
}
