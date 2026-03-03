#include "BufferElement.hpp"

LHExecutor::LHBuffer::BufferElement::BufferElement() {
  this->data = nullptr;
  this->size = 0;
}

LHExecutor::LHBuffer::BufferElement::BufferElement(uint32_t size) {
  this->data = std::make_unique<std::string>(size, '0');
  this->size = size;
}

LHExecutor::LHBuffer::BufferElement::BufferElement(
    std::pair<char*, uint32_t>&& in) {
  this->data = std::make_unique<std::string>(in.first, in.second);
  this->size = in.second;
}

LHExecutor::LHBuffer::BufferElement::BufferElement(std::string&& in) {
  this->size = in.size();
  this->data = std::make_unique<std::string>(std::move(in));
}

LHExecutor::LHBuffer::BufferElement::~BufferElement() {
  this->size = 0;
  this->data = nullptr;
}

uint32_t LHExecutor::LHBuffer::BufferElement::getSize() { return this->size; }

std::pair<const char*, uint32_t>
LHExecutor::LHBuffer::BufferElement::get_as_pair() {
  return {this->data->c_str(), this->size};
}

std::string& LHExecutor::LHBuffer::BufferElement::get_as_string() {
  return *this->data;
}

bool LHExecutor::LHBuffer::BufferElement::update(
    std::pair<char*, uint32_t>&& in) {
  if (!in.first) {
    return false;
  }

  // not needed; but I want to be sure
  if (this->size != 0 || this->data) {
    this->data = nullptr;
  }

  this->data = std::make_unique<std::string>(in.first, in.second);
  this->size = in.second;
  return true;
}

bool LHExecutor::LHBuffer::BufferElement::update(std::string&& in) {
  // not needed; but I want to be sure
  if (this->size != 0 || this->data) {
    this->data = nullptr;
  }

  this->size = in.size();
  this->data = std::make_unique<std::string>(std::move(in));
  return true;
}

LHExecutor::LHBuffer::BufferElement&
LHExecutor::LHBuffer::BufferElement::operator=(
    LHExecutor::LHBuffer::BufferElement&& other) {
  this->data = std::move(other.data);
  this->size = other.size;
  return *this;
}

LHExecutor::LHBuffer::BufferElement&
LHExecutor::LHBuffer::BufferElement::operator=(std::string&& other) {
  this->update(std::move(other));
  return *this;
}

LHExecutor::LHBuffer::BufferElement&
LHExecutor::LHBuffer::BufferElement::operator=(
    std::pair<char*, uint32_t>&& other) {
  this->update(std::move(other));
  return *this;
}

bool LHExecutor::LHBuffer::BufferElement::has_element() {
  return this->data != nullptr;
}

LHExecutor::LHBuffer::BufferElement&
LHExecutor::LHBuffer::BufferElement::operator=(
    LHExecutor::LHBuffer::BufferElement& other) {
  this->size = other.size;

  if (other.has_element()) {
    this->data = std::make_unique<std::string>(other.get_as_string());
  } else {
    this->data = nullptr;
  }
  return *this;
}

void LHExecutor::LHBuffer::BufferElement::clear() {
  this->data = nullptr;
  this->size = 0;
}