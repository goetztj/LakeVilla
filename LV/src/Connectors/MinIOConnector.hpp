#pragma once

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/s3/S3Client.h>

#include <memory>

#include "Connector.hpp"
#include "settings.hpp"

namespace StorageConnector {

struct MinIOConfig {
  Aws::String user;
  Aws::String passwd;
  Aws::String region;
  bool verifySSL = false;
  uint64_t connectTimeout;
  uint64_t requestTimeout;
  Aws::String endpointURI;
  Aws::Http::Scheme scheme = Aws::Http::Scheme::HTTP;
  Aws::String default_bucket = "";

  MinIOConfig() = default;

  ConnectorType getType() { return StorageConnector::ConnectorType::MinIO; };
};

struct MinIOConnector : public Connector {
 private:
  std::unique_ptr<Aws::S3::S3Client> minio_client = nullptr;

  MinIOConfig config;

  Aws::String default_bucket;

 public:
  static std::atomic<bool> creating_client;

  MinIOConnector(MinIOConfig* config);

  bool write(std::string path, std::string file) override;

  bool write(std::string path, const char* data, uint64_t size) override;

  bool write_atomic(std::string path, const char* data, uint64_t size) override;

  std::pair<char*, uint64_t> read(std::string path) override;

  std::unique_ptr<Container> read2(const std::string& path) override;

  void read_mock(const std::string& path) override;

  bool delete_file(std::string path) override;

  void delete_sub_directory(std::string path, uint32_t until) override;

  bool check(const std::string& path) override;

  std::pair<char*, uint64_t> list(std::string& path) override;
  std::unique_ptr<Container> list2(std::string& path) override;
  Aws::S3::Model::ListObjectsV2Outcome list2_vec(
      const std::string& path) override;

  ConnectorType getType() { return this->type; };

  ~MinIOConnector();
};
}  // namespace StorageConnector