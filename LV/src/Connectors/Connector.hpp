#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "StoreClock.hpp"

namespace StorageConnector {
enum ConnectorType { MinIO, Memory, None };

// result of a conditional (put-if-absent) write
enum class WriteStatus {
  Ok,             // object was created
  AlreadyExists,       // put-if-absent: the object exists already
  PreconditionFailed,  // put-if-match: the object changed (or is gone)
  Error                // transient or unknown error; the write may be retried
};

// version information of an object
struct ObjectVersion {
  std::string etag;
  // store time of the last modification (-1 if unknown)
  int64_t last_modified_ms = -1;
};

struct ObjectInfo {
  std::string key;
  uint64_t size = 0;
};

struct Container {
  char* data;
  uint64_t length;

  Container() {
    data = nullptr;
    length = 0;
  }

  Container(const Container&) = delete;
  Container& operator=(const Container&) = delete;

  ~Container() {
    if (data) {
      std::free(data);
    }
  }
};

struct Connector {
  ConnectorType type;
  std::atomic<uint64_t> bytes_read;
  std::atomic<uint64_t> bytes_write;

  Connector(ConnectorType type) : type(type) {
    bytes_read = 0;
    bytes_write = 0;
  }

  // uploads the local file 'file' to 'path'
  virtual bool write(std::string path, std::string file) = 0;

  virtual bool write(std::string path, const char* data, uint64_t size) = 0;

  // creates 'path' only if it does not exist yet (If-None-Match: *); etag
  // receives the ETag of the created object
  virtual WriteStatus write_atomic(std::string path, const char* data,
                                   uint64_t size,
                                   std::string* etag = nullptr) = 0;

  virtual std::pair<char*, uint64_t> read(std::string path) = 0;

  virtual std::pair<char*, uint64_t> list(std::string& path) = 0;

  virtual std::unique_ptr<Container> read2(const std::string& path) = 0;

  // reads an object together with its ETag and modification time
  virtual std::unique_ptr<Container> read_versioned(
      const std::string& path, ObjectVersion& version) = 0;

  // overwrites 'path' only if its ETag is still 'etag' (If-Match)
  virtual WriteStatus write_if_match(const std::string& path,
                                     const char* data, uint64_t size,
                                     const std::string& etag) = 0;

  virtual void read_mock(const std::string& path) = 0;

  virtual std::unique_ptr<Container> list2(std::string& path) = 0;

  // lists all objects with the given prefix (all pages); std::nullopt on error
  virtual std::optional<std::vector<ObjectInfo>> list_objects(
      const std::string& prefix) = 0;

  virtual bool check(const std::string& path) = 0;

  virtual bool delete_file(std::string path) = 0;

  virtual void delete_sub_directory(std::string path, uint32_t until) = 0;

  // current time of the object store (estimated; local clock if the store
  // does not provide one)
  virtual StoreTime store_time() { return {local_time_ms(), 0}; }

  ConnectorType getType() { return this->type; };

  uint64_t getReadsBytes() { return bytes_read; };
  uint64_t getWritesBytes() { return bytes_write; };

  virtual ~Connector() {};
};

}  // namespace StorageConnector
