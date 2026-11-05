#pragma once

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>

#include <memory>
#include <mutex>

#include "Connector.hpp"
#include "MinIOConfig.hpp"

namespace StorageConnector {

struct MinIOConnector : public Connector {
 private:
  std::unique_ptr<Aws::S3::S3Client> minio_client = nullptr;

  MinIOConfig config;

  Aws::String default_bucket;

  // offset to the store's clock, estimated from HTTP Date headers
  OffsetClock clock;

  // HeadBucket request that samples the store's Date header
  void sample_store_clock();

 public:
  // the AWS SDK does not support creating clients concurrently
  static std::mutex creating_client;

  MinIOConnector(MinIOConfig* config);

  bool write(std::string path, std::string file) override;

  bool write(std::string path, const char* data, uint64_t size) override;

  WriteStatus write_atomic(std::string path, const char* data, uint64_t size,
                           std::string* etag = nullptr) override;

  std::pair<char*, uint64_t> read(std::string path) override;

  std::unique_ptr<Container> read2(const std::string& path) override;

  std::unique_ptr<Container> read_versioned(const std::string& path,
                                            ObjectVersion& version) override;

  WriteStatus write_if_match(const std::string& path, const char* data,
                             uint64_t size, const std::string& etag) override;

  void read_mock(const std::string& path) override;

  bool delete_file(std::string path) override;

  void delete_sub_directory(std::string path, uint32_t until) override;

  bool check(const std::string& path) override;

  std::pair<char*, uint64_t> list(std::string& path) override;
  std::unique_ptr<Container> list2(std::string& path) override;
  std::optional<std::vector<ObjectInfo>> list_objects(
      const std::string& prefix) override;

  StoreTime store_time() override;

  ConnectorType getType() { return this->type; };

  ~MinIOConnector();
};
}  // namespace StorageConnector
