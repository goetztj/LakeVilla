#include "BufferManager.hpp"

LHExecutor::LHBuffer::BufferManager::BufferManager() { this->elements.clear(); }

LHExecutor::LHBuffer::BufferManager::BufferManager(uint32_t size) {
  this->elements.reserve(size);
  for (uint32_t i = 0; i < size; i++) {
    this->elements.push_back(std::make_unique<BufferElement>());
  }
}

LHExecutor::LHBuffer::BufferManager::BufferManager(
    LHExecutor::LHBuffer::BufferManager& other) {
  this->elements.reserve(other.elements.size());

  for (size_t i = 0; i < other.elements.size(); i++) {
    this->elements.push_back(std::make_unique<BufferElement>());

    if (other[i].has_element()) {
      auto elem = other[i].get_as_string();
      this->elements[i]->update(std::move(elem));
    }
  }
}

bool LHExecutor::LHBuffer::BufferManager::copy(
    LHExecutor::LHBuffer::BufferManager& other) {
  this->elements.clear();
  this->elements.reserve(other.elements.size());

  for (size_t i = 0; i < other.elements.size(); i++) {
    this->elements.push_back(std::make_unique<BufferElement>());

    if (other[i].has_element()) {
      auto elem = other[i].get_as_string();
      this->elements[i]->update(std::move(elem));
    }
  }
  return true;
}

LHExecutor::LHBuffer::BufferManager::~BufferManager() {
  this->elements.clear();
}

bool LHExecutor::LHBuffer::BufferManager::set(uint32_t index,
                                              std::string&& data) {
  while (this->elements.size() <= index) {
    this->elements.push_back(std::make_unique<BufferElement>());
  }

  this->elements[index]->update(std::move(data));
  return true;
}

bool LHExecutor::LHBuffer::BufferManager::set(
    uint32_t index, std::pair<char*, uint32_t>&& data) {
  while (this->elements.size() <= index) {
    this->elements.push_back(std::make_unique<BufferElement>());
  }

  this->elements[index]->update(std::move(data));
  return true;
}

LHExecutor::LHBuffer::BufferElement& LHExecutor::LHBuffer::BufferManager::get(
    uint32_t index) {
  while (this->elements.size() <= index) {
    this->elements.push_back(std::make_unique<BufferElement>());
  }

  return *(this->elements[index]);
}

LHExecutor::LHBuffer::BufferElement&
LHExecutor::LHBuffer::BufferManager::operator[](uint32_t index) {
  return this->get(index);
}

size_t LHExecutor::LHBuffer::BufferManager::get_size() {
  return this->elements.size();
}