#pragma once
#include <memory>
#include <string>
#include <utility>

namespace LHExecutor::LHBuffer {

struct BufferElement {
  BufferElement();
  BufferElement(uint32_t size);
  BufferElement(std::pair<char*, uint32_t>&& in);
  BufferElement(std::string&& in);

  ~BufferElement();

  uint32_t getSize();
  std::pair<const char*, uint32_t> get_as_pair();
  std::string& get_as_string();

  bool update(std::pair<char*, uint32_t>&& in);
  bool update(std::string&& in);

  void clear();

  BufferElement& operator=(BufferElement&& other);
  BufferElement& operator=(BufferElement& other);
  BufferElement& operator=(std::string&& other);
  BufferElement& operator=(std::pair<char*, uint32_t>&& other);

  bool has_element();

 private:
  std::unique_ptr<std::string> data;
  uint32_t size;
};
};  // namespace LHExecutor::LHBuffer