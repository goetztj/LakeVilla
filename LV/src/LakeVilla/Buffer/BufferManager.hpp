#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "BufferElement.hpp"

namespace LHExecutor::LHBuffer {

struct BufferManager {
  BufferManager();
  BufferManager(uint32_t size);
  BufferManager(BufferManager& other);

  bool set(uint32_t index, std::string&& data);
  bool set(uint32_t index, std::pair<char*, uint32_t>&& data);

  BufferElement& get(uint32_t index);

  bool copy(BufferManager& other);

  size_t get_size();

  BufferElement& operator[](uint32_t index);

  ~BufferManager();

 private:
  std::vector<std::unique_ptr<BufferElement>> elements;
};

};  // namespace LHExecutor::LHBuffer