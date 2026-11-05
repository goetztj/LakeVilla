#pragma once

#include <functional>
#include <map>
#include <mutex>

#include "Connector.hpp"

namespace StorageConnector {

// In-memory object store with S3 semantics (atomic PUT, put-if-absent, prefix
// listing in lexicographic order). Used for tests and local experiments.
struct MemoryConnector : public Connector {
  enum class Op { Write, WriteAtomic, WriteIfMatch, Read, List, Check, Delete };

  // called before every operation; returning true makes the operation fail
  // (fault injection for tests)
  std::function<bool(Op, const std::string&)> fail_hook;

  // store clock for tests (ms since the epoch); local clock if unset
  std::function<int64_t()> clock;
  int64_t clock_uncertainty_ms = 0;

  StoreTime store_time() override;

  MemoryConnector();

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

  // test helpers (not counted in bytes_read/bytes_write)
  void put(const std::string& path, const std::string& content);
  std::optional<std::string> get(const std::string& path);
  std::vector<std::string> keys(const std::string& prefix = "");
  void clear();

 private:
  std::mutex mutex;
  struct Object {
    std::string content;
    std::string etag;
    int64_t modified_ms = 0;
  };
  std::map<std::string, Object> objects;
  uint64_t next_etag = 0;

  // stores content under path with a new ETag; mutex must be held
  void store(const std::string& path, std::string content);

  bool should_fail(Op op, const std::string& path);
};

}  // namespace StorageConnector
