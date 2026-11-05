#include "MemoryConnector.hpp"

#include <cstring>
#include <fstream>
#include <sstream>

using StorageConnector::MemoryConnector;

MemoryConnector::MemoryConnector()
    : Connector(StorageConnector::ConnectorType::Memory) {}

bool MemoryConnector::should_fail(Op op, const std::string& path) {
  return fail_hook && fail_hook(op, path);
}

bool MemoryConnector::write(std::string path, std::string file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return false;
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  auto content = buffer.str();
  return this->write(path, content.data(), content.size());
}

void MemoryConnector::store(const std::string& path, std::string content) {
  objects[path] = {std::move(content),
                   "\"etag-" + std::to_string(++next_etag) + "\"",
                   clock ? clock() : local_time_ms()};
}

StorageConnector::StoreTime MemoryConnector::store_time() {
  return {clock ? clock() : local_time_ms(), clock_uncertainty_ms};
}

bool MemoryConnector::write(std::string path, const char* data, uint64_t size) {
  if (should_fail(Op::Write, path)) {
    return false;
  }
  std::lock_guard<std::mutex> guard(mutex);
  store(path, std::string(data, size));
  bytes_write += size;
  return true;
}

StorageConnector::WriteStatus MemoryConnector::write_atomic(std::string path,
                                                            const char* data,
                                                            uint64_t size,
                                                            std::string* etag) {
  if (should_fail(Op::WriteAtomic, path)) {
    return WriteStatus::Error;
  }
  std::lock_guard<std::mutex> guard(mutex);
  if (objects.count(path) != 0) {
    return WriteStatus::AlreadyExists;
  }
  store(path, std::string(data, size));
  if (etag) {
    *etag = objects[path].etag;
  }
  bytes_write += size;
  return WriteStatus::Ok;
}

std::pair<char*, uint64_t> MemoryConnector::read(std::string path) {
  auto container = this->read2(path);
  if (!container->data) {
    return {nullptr, 0};
  }
  std::pair<char*, uint64_t> result = {container->data, container->length};
  container->data = nullptr;
  return result;
}

std::unique_ptr<StorageConnector::Container> MemoryConnector::read2(
    const std::string& path) {
  auto result = std::make_unique<Container>();
  if (should_fail(Op::Read, path)) {
    return result;
  }
  std::lock_guard<std::mutex> guard(mutex);
  auto it = objects.find(path);
  if (it == objects.end()) {
    return result;
  }
  result->data = (char*)std::malloc(it->second.content.size() + 1);
  std::memcpy(result->data, it->second.content.data(),
              it->second.content.size());
  result->length = it->second.content.size();
  bytes_read += result->length;
  return result;
}

std::unique_ptr<StorageConnector::Container> MemoryConnector::read_versioned(
    const std::string& path, ObjectVersion& version) {
  auto result = std::make_unique<Container>();
  version = {};
  if (should_fail(Op::Read, path)) {
    return result;
  }
  std::lock_guard<std::mutex> guard(mutex);
  auto it = objects.find(path);
  if (it == objects.end()) {
    return result;
  }
  result->data = (char*)std::malloc(it->second.content.size() + 1);
  std::memcpy(result->data, it->second.content.data(),
              it->second.content.size());
  result->length = it->second.content.size();
  version.etag = it->second.etag;
  version.last_modified_ms = it->second.modified_ms;
  bytes_read += result->length;
  return result;
}

StorageConnector::WriteStatus MemoryConnector::write_if_match(
    const std::string& path, const char* data, uint64_t size,
    const std::string& etag) {
  if (should_fail(Op::WriteIfMatch, path)) {
    return WriteStatus::Error;
  }
  std::lock_guard<std::mutex> guard(mutex);
  auto it = objects.find(path);
  if (it == objects.end() || it->second.etag != etag) {
    return WriteStatus::PreconditionFailed;
  }
  store(path, std::string(data, size));
  bytes_write += size;
  return WriteStatus::Ok;
}

void MemoryConnector::read_mock(const std::string& path) {
  this->read2(path);
}

bool MemoryConnector::delete_file(std::string path) {
  if (should_fail(Op::Delete, path)) {
    return false;
  }
  std::lock_guard<std::mutex> guard(mutex);
  objects.erase(path);
  bytes_write += path.size();
  return true;
}

void MemoryConnector::delete_sub_directory(std::string path, uint32_t until) {
  for (uint32_t log_id = 0; log_id < until; log_id++) {
    std::stringstream sublog_key;
    sublog_key << path << "00" << log_id << ".sub";
    this->delete_file(sublog_key.str());
  }
}

bool MemoryConnector::check(const std::string& path) {
  if (should_fail(Op::Check, path)) {
    return false;
  }
  std::lock_guard<std::mutex> guard(mutex);
  auto it = objects.find(path);
  if (it == objects.end()) {
    return false;
  }
  bytes_read += it->second.content.size();
  return true;
}

std::optional<std::vector<StorageConnector::ObjectInfo>>
MemoryConnector::list_objects(const std::string& prefix) {
  if (should_fail(Op::List, prefix)) {
    return std::nullopt;
  }
  std::lock_guard<std::mutex> guard(mutex);
  std::vector<ObjectInfo> result;
  for (auto it = objects.lower_bound(prefix);
       it != objects.end() && it->first.compare(0, prefix.size(), prefix) == 0;
       ++it) {
    result.push_back({it->first, it->second.content.size()});
    bytes_read += it->first.size();
  }
  return result;
}

std::unique_ptr<StorageConnector::Container> MemoryConnector::list2(
    std::string& path) {
  auto result = std::make_unique<Container>();
  auto objs = this->list_objects(path);
  if (!objs) {
    return result;
  }
  std::stringstream stream;
  for (auto& obj : *objs) {
    stream << obj.key << "\n";
  }
  auto str = stream.str();
  result->data = (char*)std::malloc(str.size() + 1);
  std::memcpy(result->data, str.data(), str.size());
  result->length = str.size();
  return result;
}

std::pair<char*, uint64_t> MemoryConnector::list(std::string& path) {
  auto container = this->list2(path);
  if (!container->data) {
    return {nullptr, 0};
  }
  std::pair<char*, uint64_t> result = {container->data, container->length};
  container->data = nullptr;
  return result;
}

void MemoryConnector::put(const std::string& path, const std::string& content) {
  std::lock_guard<std::mutex> guard(mutex);
  store(path, content);
}

std::optional<std::string> MemoryConnector::get(const std::string& path) {
  std::lock_guard<std::mutex> guard(mutex);
  auto it = objects.find(path);
  if (it == objects.end()) {
    return std::nullopt;
  }
  return it->second.content;
}

std::vector<std::string> MemoryConnector::keys(const std::string& prefix) {
  std::lock_guard<std::mutex> guard(mutex);
  std::vector<std::string> result;
  for (auto it = objects.lower_bound(prefix);
       it != objects.end() && it->first.compare(0, prefix.size(), prefix) == 0;
       ++it) {
    result.push_back(it->first);
  }
  return result;
}

void MemoryConnector::clear() {
  std::lock_guard<std::mutex> guard(mutex);
  objects.clear();
}
