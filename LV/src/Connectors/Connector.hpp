#pragma once
#include <aws/core/Aws.h>

#include <string>
#include <utility>

namespace StorageConnector {
enum ConnectorType { MinIO, None };

struct Container {
  char* data;
  uint64_t length;

  Container() {
    data = nullptr;
    length = 0;
  }

  ~Container() {
    if (data) {
      std::free(data);
    }
  }
};

struct Connector {
  ConnectorType type;
  uint64_t bytes_read;
  uint64_t bytes_write;

  Connector(ConnectorType type) : type(type) {
    bytes_read = 0;
    bytes_write = 0;
  }

  virtual bool write(std::string path, std::string file) = 0;

  virtual bool write(std::string path, const char* data, uint64_t size) = 0;

  virtual bool write_atomic(std::string path, const char* data,
                            uint64_t size) = 0;

  virtual std::pair<char*, uint64_t> read(std::string path) = 0;

  virtual std::pair<char*, uint64_t> list(std::string& path) = 0;

  virtual std::unique_ptr<Container> read2(const std::string& path) = 0;

  virtual void read_mock(const std::string& path) = 0;

  virtual std::unique_ptr<Container> list2(std::string& path) = 0;

  virtual Aws::S3::Model::ListObjectsV2Outcome list2_vec(
      const std::string& path) = 0;

  virtual bool check(const std::string& path) = 0;

  virtual bool delete_file(std::string path) = 0;

  virtual void delete_sub_directory(std::string path, uint32_t until) = 0;

  ConnectorType getType() { return this->type; };

  uint64_t getReadsBytes() { return bytes_read; };
  uint64_t getWritesBytes() { return bytes_write; };

  virtual ~Connector() {};
};

}  // namespace StorageConnector